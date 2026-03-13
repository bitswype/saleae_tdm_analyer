"""CLI for the TDM Audio Stream test harness.

Provides commands to serve test signals through the HLA and to run
automated verification — all without requiring Logic 2 or hardware.
"""

import json
import logging
import signal
import sys
import threading
import time

import click

from .signals import parse_signal_spec, from_wav
from .frame_emitter import emit_frames
from .hla_driver import HlaDriver
from .verifier import Verifier

log = logging.getLogger('tdm-test-harness')


def _make_slot_list(channels):
    """Generate a slot list [0, 1, ..., channels-1]."""
    return list(range(channels))


def _make_slot_spec(channels):
    """Generate a slot spec string like '0,1,2,3'."""
    return ','.join(str(i) for i in range(channels))


def _feed_with_pacing(driver, frames, sample_rate, slot_count, realtime=True,
                      chunk_ms=5):
    """Feed frames to the HLA, optionally pacing at real-time speed.

    When realtime=True, groups frames into chunks of chunk_ms milliseconds
    and sleeps between chunks to approximate real-time delivery. Chunking
    avoids per-frame sleeps (Python sleep granularity is ~1-10ms, far too
    coarse for per-sample pacing at 48kHz). Smaller chunks (5ms default)
    produce smoother delivery for audio playback.

    When False, feeds as fast as possible.
    """
    # Group all slot-frames by TDM frame number
    tdm_groups = []
    current_group = []
    current_fn = None

    for frame in frames:
        fn = frame.data['frame_number']
        if current_fn is not None and fn != current_fn:
            tdm_groups.append(current_group)
            current_group = []
        current_group.append(frame)
        current_fn = fn
    if current_group:
        tdm_groups.append(current_group)

    if not realtime:
        # Feed everything at once
        driver.feed(frames)
        return

    # Feed in time-aligned chunks at slightly faster than real-time.
    # The 0.95 factor means we feed ~5% faster, which keeps the player's
    # buffer topped up and absorbs scheduling jitter. The ring buffer's
    # deque(maxlen=N) prevents runaway growth.
    pace_factor = 0.95
    chunk_size = max(1, int(sample_rate * chunk_ms / 1000))
    start_time = time.monotonic()
    groups_fed = 0

    for i in range(0, len(tdm_groups), chunk_size):
        chunk = tdm_groups[i:i + chunk_size]
        flat = [f for group in chunk for f in group]
        driver.feed(flat)
        groups_fed += len(chunk)

        # Sleep until this chunk's adjusted deadline
        expected_time = start_time + (groups_fed / sample_rate) * pace_factor
        now = time.monotonic()
        if expected_time > now:
            time.sleep(expected_time - now)


def _feed_batched(driver, frames, batch_size):
    """Feed frames in batches, yielding between batches to let the sender drain.

    Splits the frame list into chunks of TDM frames (detected by frame_number
    boundaries). Each chunk contains up to batch_size TDM frames. A short sleep
    between chunks gives the sender thread time to drain the ring buffer.

    This prevents ring buffer overflow when feeding faster than real-time,
    while still being much faster than real-time pacing.
    """
    # Split into per-TDM-frame groups
    tdm_frames = []
    current_group = []
    current_fn = None

    for frame in frames:
        fn = frame.data['frame_number']
        if current_fn is not None and fn != current_fn:
            tdm_frames.append(current_group)
            current_group = []
        current_group.append(frame)
        current_fn = fn
    if current_group:
        tdm_frames.append(current_group)

    # Feed in batches, sleeping proportionally to let the sender drain.
    # Each PCM frame is (channels * bytes_per_sample) bytes sent over TCP.
    # At ~100 MB/s localhost throughput the sender can drain fast, but we
    # also need the sender thread to wake and run between batches.
    for i in range(0, len(tdm_frames), batch_size):
        chunk = tdm_frames[i:i + batch_size]
        flat = [f for group in chunk for f in group]
        driver.feed(flat)
        if i + batch_size < len(tdm_frames):
            # Sleep long enough for the sender thread to drain this batch.
            # 0.001s per frame in the batch, clamped to [0.005, 0.1]s.
            sleep_time = max(0.005, min(0.1, len(chunk) * 0.001))
            time.sleep(sleep_time)


@click.group()
@click.option('-v', '--verbose', count=True, help='Increase verbosity (-v info, -vv debug)')
@click.help_option('-h', '--help')
@click.pass_context
def cli(ctx, verbose):
    """TDM Audio Stream test harness.

    Test the TDM Audio Stream HLA without Logic 2 or hardware.
    Feed test signals, verify PCM output, and iterate quickly.
    """
    level = {0: logging.WARNING, 1: logging.INFO}.get(verbose, logging.DEBUG)
    logging.basicConfig(
        level=level,
        format='%(levelname)s: %(message)s',
    )
    ctx.ensure_object(dict)
    ctx.obj['verbose'] = verbose


@cli.command()
@click.option('--signal', 'signal_spec', default='sine:440',
              help='Signal spec: sine:<freq>, sine:<f1>,<f2>,..., silence, ramp')
@click.option('--wav-file', type=click.Path(exists=True),
              help='WAV file to use as source (overrides --signal)')
@click.option('--sample-rate', default=48000, type=int, help='Sample rate in Hz')
@click.option('--channels', default=2, type=int, help='Number of channels')
@click.option('--bit-depth', default='16', type=click.Choice(['16', '32']),
              help='Bit depth')
@click.option('--port', default=4011, type=int, help='TCP port')
@click.option('--buffer-size', default=4096, type=int, help='Ring buffer size in frames')
@click.option('--duration', default=10.0, type=float,
              help='Duration in seconds (0 = infinite)')
@click.option('--loop/--no-loop', default=True, help='Loop the signal')
@click.help_option('-h', '--help')
def serve(signal_spec, wav_file, sample_rate, channels, bit_depth, port,
          buffer_size, duration, loop):
    """Start the HLA with a test signal and stream over TCP.

    The HLA's TCP server listens for connections. Use tdm-audio-bridge
    or any TCP client to receive the PCM stream.

    Examples:

        tdm-test-harness serve --signal sine:440

        tdm-test-harness serve --signal sine:440,880 --channels 2

        tdm-test-harness serve --wav-file test.wav
    """
    bit_depth_int = int(bit_depth)

    # Generate signal
    if wav_file:
        signal_data, sample_rate, channels = from_wav(wav_file, bit_depth_int)
        log.info("Loaded WAV: %d frames, %d Hz, %d ch", len(signal_data),
                 sample_rate, channels)
    else:
        sig_duration = max(duration, 1.0) if duration > 0 else 10.0
        signal_data = parse_signal_spec(
            signal_spec, sample_rate, channels, sig_duration, bit_depth_int
        )
        log.info("Generated %s: %d frames, %d Hz, %d ch",
                 signal_spec, len(signal_data), sample_rate, channels)

    slot_spec = _make_slot_spec(channels)
    slot_list = _make_slot_list(channels)

    # Create HLA
    driver = HlaDriver(slot_spec, port=port, buffer_size=buffer_size,
                        bit_depth=bit_depth_int)

    click.echo(f"TCP server listening on 127.0.0.1:{port}")
    click.echo(f"Signal: {signal_spec if not wav_file else wav_file}")
    click.echo(f"Format: {channels}ch, {sample_rate}Hz, {bit_depth}-bit")
    click.echo("Press Ctrl+C to stop.")

    stop = threading.Event()

    def sigint_handler(sig, frame):
        click.echo("\nStopping...")
        stop.set()

    signal.signal(signal.SIGINT, sigint_handler)

    try:
        frame_num = 0
        while not stop.is_set():
            frames = list(emit_frames(signal_data, sample_rate, slot_list,
                                       start_frame_num=frame_num))
            # Add one extra frame to flush the last TDM frame
            from .frame_emitter import FakeFrame
            flush_t = (frame_num + len(signal_data)) / sample_rate
            flush_frame = FakeFrame(
                frame_type='slot',
                start_time=flush_t,
                end_time=flush_t,
                data={
                    'slot': slot_list[0],
                    'data': 0,
                    'frame_number': frame_num + len(signal_data),
                    'severity': 'ok',
                    'short_slot': False,
                    'bitclock_error': False,
                },
            )
            frames.append(flush_frame)

            _feed_with_pacing(driver, frames, sample_rate, channels,
                               realtime=True)

            frame_num += len(signal_data) + 1

            if not loop or (duration > 0 and frame_num / sample_rate >= duration):
                # Wait for any remaining data to be sent
                time.sleep(0.5)
                break

    except KeyboardInterrupt:
        pass
    finally:
        driver.shutdown()
        click.echo("Shut down.")


@cli.command()
@click.option('--signal', 'signal_spec', default='sine:440',
              help='Signal spec: sine:<freq>, sine:<f1>,<f2>,..., silence, ramp')
@click.option('--wav-file', type=click.Path(exists=True),
              help='WAV file to use as source (overrides --signal)')
@click.option('--sample-rate', default=48000, type=int, help='Sample rate in Hz')
@click.option('--channels', default=2, type=int, help='Number of channels')
@click.option('--bit-depth', default='16', type=click.Choice(['16', '32']),
              help='Bit depth')
@click.option('--port', default=14099, type=int,
              help='TCP port (default 14099 to avoid conflicts)')
@click.option('--buffer-size', default=0, type=int,
              help='Ring buffer size (0 = auto-size to fit all frames)')
@click.option('--duration', default=1.0, type=float, help='Duration in seconds')
@click.option('--json', 'json_output', is_flag=True,
              help='Output results as JSON (agent-friendly)')
@click.option('--tolerance', default=1, type=int,
              help='Max allowed per-sample absolute error')
@click.help_option('-h', '--help')
def verify(signal_spec, wav_file, sample_rate, channels, bit_depth, port,
           buffer_size, duration, json_output, tolerance):
    """Run automated verification of the HLA.

    Generates a test signal, feeds it through the HLA, connects a TCP
    client, and compares received PCM against expected output.

    Exit code 0 = pass, 1 = fail.

    Examples:

        tdm-test-harness verify --signal sine:440 --duration 0.5 --json

        tdm-test-harness verify --signal sine:440,880 --channels 2 --json

        tdm-test-harness verify --wav-file test.wav --json
    """
    bit_depth_int = int(bit_depth)

    # Generate signal
    if wav_file:
        signal_data, sample_rate, channels = from_wav(wav_file, bit_depth_int)
    else:
        signal_data = parse_signal_spec(
            signal_spec, sample_rate, channels, duration, bit_depth_int
        )

    slot_spec = _make_slot_spec(channels)
    slot_list = _make_slot_list(channels)

    # Auto-size buffer to fit all frames plus headroom
    if buffer_size <= 0:
        buffer_size = len(signal_data) + 64

    log.info("Test: %d frames, %d Hz, %d ch, %d-bit, port %d",
             len(signal_data), sample_rate, channels, bit_depth_int, port)

    # Create HLA
    driver = HlaDriver(slot_spec, port=port, buffer_size=buffer_size,
                        bit_depth=bit_depth_int)

    try:
        # Phase 1: Feed warmup frames to derive sample rate
        # Need at least 2 TDM frames for the same slot to appear twice
        warmup = [[0] * channels for _ in range(3)]
        warmup_frames = list(emit_frames(warmup, sample_rate, slot_list))
        driver.feed(warmup_frames)

        # Wait for TCP server to be ready
        time.sleep(0.1)

        # Phase 2: Connect verifier (ring buffer cleared on connect)
        v = Verifier(host='127.0.0.1', port=port, timeout=5.0)

        # Phase 3: Feed test signal in background thread
        test_frames = list(emit_frames(signal_data, sample_rate, slot_list,
                                        start_frame_num=len(warmup)))

        # Add flush frame to trigger enqueue of last test frame
        from .frame_emitter import FakeFrame
        flush_fn = len(warmup) + len(signal_data)
        flush_t = flush_fn / sample_rate
        flush_frame = FakeFrame(
            frame_type='slot',
            start_time=flush_t,
            end_time=flush_t,
            data={
                'slot': slot_list[0],
                'data': 0,
                'frame_number': flush_fn,
                'severity': 'ok',
                'short_slot': False,
                'bitclock_error': False,
            },
        )
        test_frames.append(flush_frame)

        def feed_test():
            time.sleep(0.15)  # Let verifier connect first
            # Feed in batches to avoid overflowing the ring buffer.
            # Batch size is half the buffer, capped at 256 to keep
            # drain sleeps short and prevent overflow.
            batch = max(1, min(256, buffer_size // 2))
            _feed_batched(driver, test_frames, batch)

        feeder = threading.Thread(target=feed_test)
        feeder.start()

        # Phase 4: Verify
        # skip_first=1 to skip the warmup residual frame
        results = v.verify(signal_data, tolerance=tolerance, skip_first=1)

        feeder.join(timeout=10.0)

    finally:
        driver.shutdown()

    # Output results
    if json_output:
        click.echo(json.dumps(results, indent=2))
    else:
        if results['pass']:
            click.echo(click.style('PASS', fg='green', bold=True))
        else:
            click.echo(click.style('FAIL', fg='red', bold=True))
            if results.get('error'):
                click.echo(f"  Error: {results['error']}")

        click.echo(f"  Frames: {results['frames_received']} received, "
                   f"{results['frames_expected']} expected, "
                   f"{results['frames_compared']} compared")
        click.echo(f"  Max error: {results['max_error']} (tolerance: {tolerance})")
        if results.get('mismatches'):
            click.echo(f"  Mismatches: {results['mismatches']}")
        if results.get('sample_rate_derived'):
            click.echo(f"  Sample rate: {results['sample_rate_derived']} Hz")

    sys.exit(0 if results['pass'] else 1)


@cli.command()
def signals():
    """List available test signal types."""
    click.echo("Available test signals:\n")
    click.echo("  sine:<freq>          Sine wave at <freq> Hz on all channels")
    click.echo("  sine:<f1>,<f2>,...   Per-channel sine frequencies")
    click.echo("  silence              All zeros")
    click.echo("  ramp                 Linear ramp from -max to +max")
    click.echo("")
    click.echo("Examples:")
    click.echo("  --signal sine:440")
    click.echo("  --signal sine:440,880,1320,1760")
    click.echo("  --signal silence")
    click.echo("  --signal ramp")
    click.echo("")
    click.echo("Or use --wav-file <path> to read from a WAV file.")


def main():
    cli()


if __name__ == '__main__':
    main()
