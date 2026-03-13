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
                      chunk_ms=None):
    """Feed frames to the HLA, optionally pacing at real-time speed.

    When realtime=True, groups frames into chunks of chunk_ms milliseconds
    and sleeps between chunks to approximate real-time delivery. Chunking
    avoids per-frame sleeps (Python sleep granularity is ~1-10ms, far too
    coarse for per-sample pacing at 48kHz).

    If chunk_ms is None, it auto-scales based on sample rate: higher rates
    use larger chunks to give Python enough time to process decode() calls
    between sleeps.

    When False, feeds as fast as possible.
    """
    if chunk_ms is None:
        # Target ~480 TDM frames per chunk regardless of sample rate.
        # At 48kHz this is 10ms, at 96kHz it's 5ms — keeping the number
        # of decode() calls per chunk constant so Python can keep up.
        # The player's 500ms pre-buffer absorbs delivery jitter.
        chunk_ms = max(5, int(480 * 1000 / sample_rate))
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
        from .frame_emitter import FakeFrame
        frame_num = 0
        total_frames = int(sample_rate * duration) if duration > 0 else None

        # Stream signal in loop-sized chunks with continuous frame numbers.
        # No flush frames between loops — the next loop's first frame
        # triggers the flush of the previous loop's last frame.
        # This avoids pre-generating the entire signal in memory and
        # ensures data starts flowing to TCP immediately.
        while not stop.is_set():
            chunk = list(emit_frames(signal_data, sample_rate, slot_list,
                                      start_frame_num=frame_num))
            _feed_with_pacing(driver, chunk, sample_rate, channels,
                               realtime=True)
            frame_num += len(signal_data)

            if not loop:
                break
            if total_frames and frame_num >= total_frames:
                break

        # Flush the last accumulated frame
        flush_t = frame_num / sample_rate
        driver.feed([FakeFrame(
            frame_type='slot', start_time=flush_t, end_time=flush_t,
            data={'slot': slot_list[0], 'data': 0,
                  'frame_number': frame_num, 'severity': 'ok',
                  'short_slot': False, 'bitclock_error': False},
        )])
        time.sleep(0.5)

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
@click.option('--port', default=4011, type=int, help='TCP port to connect to')
@click.option('--duration', default=3.0, type=float, help='Capture duration in seconds')
@click.option('--output', '-o', default=None, type=click.Path(),
              help='Output WAV path (default: capture_<rate>Hz_<ch>ch.wav)')
@click.option('--skip', default=0.0, type=float,
              help='Seconds to skip at start (filter transients, pre-buffer)')
@click.help_option('-h', '--help')
def capture(port, duration, output, skip):
    """Capture TCP PCM stream to a WAV file.

    Connects to a running serve instance, receives the PCM stream via
    the TCP protocol, and writes it to a WAV file for offline analysis.

    This bypasses audio hardware entirely — useful for testing the
    HLA + TCP pipeline in isolation.

    Examples:

        tdm-test-harness capture --port 4011 --duration 3

        tdm-test-harness capture -o test.wav --duration 5 --skip 0.5
    """
    import socket
    import struct
    import wave

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10.0)

    try:
        click.echo(f"Connecting to 127.0.0.1:{port}...")
        sock.connect(('127.0.0.1', port))
    except (ConnectionRefusedError, OSError) as e:
        click.echo(click.style(f"Connection failed: {e}", fg='red'))
        sys.exit(1)

    try:
        # Read handshake
        buf = b''
        while b'\n' not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                click.echo(click.style("Connection closed before handshake", fg='red'))
                sys.exit(1)
            buf += chunk

        line, remainder = buf.split(b'\n', 1)
        handshake = json.loads(line)

        sample_rate = handshake['sample_rate']
        channels = handshake['channels']
        bit_depth = handshake['bit_depth']
        bytes_per_sample = bit_depth // 8
        frame_size = channels * bytes_per_sample

        click.echo(f"Handshake: {channels}ch, {sample_rate}Hz, {bit_depth}-bit")

        # Determine output path
        wav_path = output or f"capture_{sample_rate}Hz_{channels}ch.wav"

        # Calculate how many bytes to capture
        total_frames = int(sample_rate * (duration + skip))
        skip_frames = int(sample_rate * skip)
        total_bytes = total_frames * frame_size

        click.echo(f"Capturing {duration}s (skipping first {skip}s)...")

        # Receive PCM data
        pcm_buf = remainder
        sock.settimeout(2.0)
        while len(pcm_buf) < total_bytes:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            pcm_buf += chunk

        # Skip the initial transient frames
        skip_bytes = skip_frames * frame_size
        pcm_data = pcm_buf[skip_bytes:skip_bytes + int(sample_rate * duration) * frame_size]

        # Align to frame boundary
        pcm_data = pcm_data[:len(pcm_data) - (len(pcm_data) % frame_size)]

        captured_frames = len(pcm_data) // frame_size
        captured_duration = captured_frames / sample_rate

        # Write WAV
        with wave.open(wav_path, 'wb') as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(bytes_per_sample)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm_data)

        click.echo(f"Wrote {wav_path}: {captured_frames} frames ({captured_duration:.3f}s)")

    finally:
        sock.close()


@cli.command()
@click.argument('wav_file', type=click.Path(exists=True))
@click.option('--freq', type=float, default=None,
              help='Expected frequency in Hz (enables notch filter analysis)')
@click.option('--window-ms', default=50, type=int,
              help='Analysis window size in milliseconds')
@click.option('--threshold-db', default=-40.0, type=float,
              help='Residual RMS threshold in dB (windows above this are glitches)')
@click.option('--skip-start-ms', default=100, type=int,
              help='Skip initial ms (filter startup transient)')
@click.option('--json', 'json_output', is_flag=True,
              help='Output results as JSON')
@click.help_option('-h', '--help')
def analyze(wav_file, freq, window_ms, threshold_db, skip_start_ms, json_output):
    """Analyze a WAV file for audio quality using sox.

    Checks for glitches, dropouts, and signal integrity. If --freq is
    given, applies a notch filter to isolate non-signal content and
    detects anomalous windows.

    Requires sox to be installed.

    Examples:

        tdm-test-harness analyze capture.wav --freq 440

        tdm-test-harness analyze capture.wav --freq 440 --json

        tdm-test-harness analyze capture.wav --freq 440 --threshold-db -35
    """
    import subprocess
    import shutil

    sox_path = shutil.which('sox')
    if not sox_path:
        click.echo(click.style("sox not found — install with: sudo apt install sox", fg='red'))
        sys.exit(1)

    soxi_path = shutil.which('soxi')

    results = {'file': wav_file, 'pass': True, 'checks': []}

    def run_sox(args, stderr=True):
        """Run sox and return its stderr output (where stats go)."""
        cmd = [sox_path] + args
        log.debug("Running: %s", ' '.join(cmd))
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return proc.stderr if stderr else proc.stdout

    def parse_stat(output):
        """Parse sox stat output into a dict."""
        stats = {}
        for line in output.strip().split('\n'):
            if ':' in line:
                # stats output format: "Key         value"
                # stat output format: "Key           value"
                parts = line.rsplit(None, 1)
                if len(parts) == 2:
                    key = parts[0].strip().rstrip(':').strip()
                    try:
                        stats[key] = float(parts[1])
                    except ValueError:
                        stats[key] = parts[1]
            elif line.strip():
                # stat -freq format: "freq  magnitude"
                pass
        return stats

    def parse_stats(output):
        """Parse sox stats output into a dict."""
        stats = {}
        for line in output.strip().split('\n'):
            parts = line.rsplit(None, 1)
            if len(parts) == 2:
                key = parts[0].strip()
                try:
                    stats[key] = float(parts[1])
                except ValueError:
                    stats[key] = parts[1]
        return stats

    # === Check 1: File info ===
    info_output = run_sox([wav_file, '-n', 'stat'])
    stat = parse_stat(info_output)
    stats_output = run_sox([wav_file, '-n', 'stats'])
    stats = parse_stats(stats_output)

    file_info = {
        'sample_rate': int(stat.get('Samples read', 0) / stat.get('Length (seconds)', 1)),
        'duration': stat.get('Length (seconds)', 0),
        'rms_amplitude': stat.get('RMS     amplitude', 0),
        'rms_db': stats.get('RMS lev dB', 0),
        'peak_db': stats.get('Pk lev dB', 0),
        'rms_trough_db': stats.get('RMS Tr dB', 0),
    }

    # Use soxi for more reliable file info
    if soxi_path:
        for flag, key in [('-r', 'sample_rate'), ('-c', 'channels'),
                          ('-b', 'bit_depth'), ('-D', 'duration')]:
            try:
                proc = subprocess.run([soxi_path, flag, wav_file],
                                       capture_output=True, text=True, timeout=10)
                val = proc.stdout.strip()
                if key == 'duration':
                    file_info[key] = float(val)
                else:
                    file_info[key] = int(val)
            except (subprocess.TimeoutExpired, ValueError):
                pass

    results['file_info'] = file_info

    # === Check 2: Rough frequency ===
    rough_freq = stat.get('Rough   frequency')
    if rough_freq is not None:
        results['rough_frequency'] = rough_freq
        if freq:
            freq_error = abs(rough_freq - freq)
            freq_ok = freq_error <= max(5, freq * 0.02)  # within 2% or 5 Hz
            results['checks'].append({
                'name': 'frequency',
                'pass': freq_ok,
                'expected': freq,
                'measured': rough_freq,
                'error_hz': freq_error,
            })
            if not freq_ok:
                results['pass'] = False

    # === Check 3: Signal level ===
    rms_db = stats.get('RMS lev dB', -100)
    level_ok = rms_db > -20  # signal should be reasonably loud
    results['checks'].append({
        'name': 'signal_level',
        'pass': level_ok,
        'rms_db': rms_db,
    })
    if not level_ok:
        results['pass'] = False

    # === Check 4: Notch filter + windowed analysis (if freq given) ===
    if freq:
        import tempfile
        import os

        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tmp:
            notched_path = tmp.name

        try:
            # Apply notch filter at the expected frequency
            run_sox([wav_file, notched_path, 'bandreject', str(freq), '5q'])

            # Get total duration
            total_duration = file_info.get('duration', 0)
            if total_duration <= 0:
                click.echo(click.style("Cannot determine file duration", fg='red'))
                sys.exit(1)

            # Analyze in windows
            window_s = window_ms / 1000.0
            skip_s = skip_start_ms / 1000.0
            window_results = []
            glitch_windows = []

            t = skip_s
            while t + window_s <= total_duration:
                win_stats = run_sox([notched_path, '-n',
                                      'trim', str(t), str(window_s), 'stats'])
                ws = parse_stats(win_stats)
                rms = ws.get('RMS lev dB', -100)
                pk = ws.get('Pk lev dB', -100)

                is_glitch = rms > threshold_db
                window_results.append({
                    'start_s': round(t, 4),
                    'rms_db': rms,
                    'pk_db': pk,
                    'glitch': is_glitch,
                })
                if is_glitch:
                    glitch_windows.append(round(t, 4))

                t += window_s

            n_windows = len(window_results)
            n_glitches = len(glitch_windows)

            # Compute noise floor (median of non-glitch windows)
            clean_rms = sorted([w['rms_db'] for w in window_results if not w['glitch']])
            noise_floor = clean_rms[len(clean_rms) // 2] if clean_rms else -100

            results['checks'].append({
                'name': 'glitch_detection',
                'pass': n_glitches == 0,
                'windows_analyzed': n_windows,
                'glitch_count': n_glitches,
                'glitch_windows': glitch_windows,
                'noise_floor_db': round(noise_floor, 1),
                'threshold_db': threshold_db,
            })
            if n_glitches > 0:
                results['pass'] = False

            # Store all windows for detailed analysis
            results['windows'] = window_results

        finally:
            try:
                os.unlink(notched_path)
            except OSError:
                pass

    # === Check 5: Dropout detection via silence effect ===
    if freq:
        import tempfile
        import os

        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tmp:
            nonsilent_path = tmp.name

        try:
            # Remove silence regions and measure remaining duration
            run_sox([wav_file, nonsilent_path,
                     'silence', '1', '100', '0.1%', '1', '100', '0.1%',
                     ':', 'restart'])

            if soxi_path:
                proc = subprocess.run([soxi_path, '-D', nonsilent_path],
                                       capture_output=True, text=True, timeout=10)
                nonsilent_duration = float(proc.stdout.strip())
            else:
                ns_out = run_sox([nonsilent_path, '-n', 'stat'])
                ns_stat = parse_stat(ns_out)
                nonsilent_duration = ns_stat.get('Length (seconds)', 0)

            total_dur = file_info.get('duration', 0)
            if total_dur > 0:
                dropout_ratio = 1.0 - (nonsilent_duration / total_dur)
                dropout_ok = dropout_ratio < 0.02  # less than 2% silence
                results['checks'].append({
                    'name': 'dropout_detection',
                    'pass': dropout_ok,
                    'total_duration': round(total_dur, 4),
                    'nonsilent_duration': round(nonsilent_duration, 4),
                    'dropout_ratio': round(dropout_ratio, 4),
                })
                if not dropout_ok:
                    results['pass'] = False
        finally:
            try:
                os.unlink(nonsilent_path)
            except OSError:
                pass

    # === Output ===
    if json_output:
        click.echo(json.dumps(results, indent=2))
    else:
        status = click.style('PASS', fg='green', bold=True) if results['pass'] \
            else click.style('FAIL', fg='red', bold=True)
        click.echo(f"{status}  {wav_file}")
        click.echo(f"  Format: {file_info.get('channels', '?')}ch, "
                   f"{file_info.get('sample_rate', '?')}Hz, "
                   f"{file_info.get('bit_depth', '?')}-bit, "
                   f"{file_info.get('duration', 0):.3f}s")
        click.echo(f"  Level: RMS={file_info.get('rms_db', '?')} dB, "
                   f"Peak={file_info.get('peak_db', '?')} dB")

        for check in results['checks']:
            icon = click.style('OK', fg='green') if check['pass'] \
                else click.style('FAIL', fg='red')
            name = check['name']

            if name == 'frequency':
                click.echo(f"  [{icon}] Frequency: {check['measured']} Hz "
                           f"(expected {check['expected']} Hz, error {check['error_hz']} Hz)")
            elif name == 'signal_level':
                click.echo(f"  [{icon}] Signal level: {check['rms_db']} dB RMS")
            elif name == 'glitch_detection':
                click.echo(f"  [{icon}] Glitch detection: {check['glitch_count']} glitches "
                           f"in {check['windows_analyzed']} windows "
                           f"(floor={check['noise_floor_db']} dB, "
                           f"threshold={check['threshold_db']} dB)")
                if check['glitch_windows']:
                    for t in check['glitch_windows'][:10]:
                        click.echo(f"         glitch at {t}s")
                    if len(check['glitch_windows']) > 10:
                        click.echo(f"         ... and {len(check['glitch_windows']) - 10} more")
            elif name == 'dropout_detection':
                click.echo(f"  [{icon}] Dropout: {check['dropout_ratio']*100:.1f}% silence "
                           f"({check['nonsilent_duration']:.3f}s / {check['total_duration']:.3f}s)")

    sys.exit(0 if results['pass'] else 1)


@cli.command('quality-sweep')
@click.option('--json', 'json_output', is_flag=True, help='Output results as JSON')
@click.help_option('-h', '--help')
@click.pass_context
def quality_sweep(ctx, json_output):
    """Run a full serve→capture→analyze sweep across configurations.

    Tests the complete HLA + TCP pipeline at multiple sample rates,
    channel counts, and bit depths. Each test starts a serve instance,
    captures the TCP stream to a WAV file, then runs sox-based quality
    analysis.

    Requires sox to be installed.

    Examples:

        tdm-test-harness quality-sweep

        tdm-test-harness quality-sweep --json
    """
    import subprocess
    import shutil
    import tempfile
    import os

    sox_path = shutil.which('sox')
    if not sox_path:
        click.echo(click.style("sox not found — install with: sudo apt install sox", fg='red'))
        sys.exit(1)

    configs = [
        # (signal, sample_rate, channels, bit_depth, freq_to_check, loop, description)
        ('sine:440', 24000, 1, '16', 440, False, '24kHz mono 16-bit'),
        ('sine:440', 44100, 1, '16', 440, False, '44.1kHz mono 16-bit'),
        ('sine:440', 48000, 1, '16', 440, False, '48kHz mono 16-bit'),
        ('sine:440', 96000, 1, '16', 440, False, '96kHz mono 16-bit'),
        ('sine:440,880', 48000, 2, '16', None, False, '48kHz stereo 16-bit'),
        ('sine:440', 48000, 1, '32', 440, False, '48kHz mono 32-bit'),
        ('sine:440,880,1320,1760', 48000, 4, '16', None, False, '48kHz 4ch 16-bit'),
        ('sine:440', 48000, 1, '16', 440, True, '48kHz mono 16-bit looping'),
        ('sine:440,880', 48000, 2, '16', None, True, '48kHz stereo 16-bit looping'),
    ]

    base_port = 14070
    all_results = []
    pass_count = 0
    fail_count = 0

    for i, (sig, rate, channels, depth, freq, loop, desc) in enumerate(configs):
        port = base_port + i
        click.echo(f"\n[{i+1}/{len(configs)}] {desc} ...")

        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tmp:
            wav_path = tmp.name

        try:
            # Start serve in background
            serve_cmd = [
                sys.executable, '-m', 'tdm_test_harness.cli',
                'serve', '--signal', sig,
                '--sample-rate', str(rate), '--channels', str(channels),
                '--bit-depth', depth, '--port', str(port),
                '--duration', '5',
                '--loop' if loop else '--no-loop',
            ]
            serve_proc = subprocess.Popen(
                serve_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            time.sleep(1.5)  # Let serve initialize

            # Capture
            capture_cmd = [
                sys.executable, '-m', 'tdm_test_harness.cli',
                'capture', '--port', str(port), '--duration', '2',
                '--skip', '0.5', '-o', wav_path,
            ]
            cap_proc = subprocess.run(
                capture_cmd, capture_output=True, text=True, timeout=15,
            )

            # Kill serve
            serve_proc.terminate()
            serve_proc.wait(timeout=5)

            if cap_proc.returncode != 0:
                result = {'config': desc, 'pass': False, 'error': cap_proc.stderr.strip()}
                all_results.append(result)
                fail_count += 1
                click.echo(click.style(f"  FAIL (capture): {result['error']}", fg='red'))
                continue

            # For multi-channel signals, analyze each channel separately
            if channels > 1 and freq is None:
                # Parse per-channel frequencies from signal spec
                freqs_str = sig.split(':')[1]
                freqs = [float(f) for f in freqs_str.split(',')]

                config_pass = True
                chan_results = []
                for ch_idx in range(channels):
                    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as ch_tmp:
                        ch_path = ch_tmp.name
                    try:
                        subprocess.run(
                            [sox_path, wav_path, ch_path, 'remix', str(ch_idx + 1)],
                            capture_output=True, timeout=10,
                        )
                        analyze_cmd = [
                            sys.executable, '-m', 'tdm_test_harness.cli',
                            'analyze', ch_path, '--freq', str(freqs[ch_idx]), '--json',
                        ]
                        a_proc = subprocess.run(
                            analyze_cmd, capture_output=True, text=True, timeout=30,
                        )
                        ch_result = json.loads(a_proc.stdout) if a_proc.stdout.strip() else {'pass': False}
                        chan_results.append({'channel': ch_idx, 'freq': freqs[ch_idx], **ch_result})
                        if not ch_result.get('pass', False):
                            config_pass = False
                    finally:
                        try:
                            os.unlink(ch_path)
                        except OSError:
                            pass

                result = {'config': desc, 'pass': config_pass, 'channels': chan_results}
            else:
                # Single channel or known single frequency
                analyze_cmd = [
                    sys.executable, '-m', 'tdm_test_harness.cli',
                    'analyze', wav_path, '--json',
                ]
                if freq:
                    analyze_cmd.extend(['--freq', str(freq)])

                a_proc = subprocess.run(
                    analyze_cmd, capture_output=True, text=True, timeout=30,
                )
                result = json.loads(a_proc.stdout) if a_proc.stdout.strip() else {'pass': False}
                result['config'] = desc

            all_results.append(result)
            if result.get('pass', False):
                pass_count += 1
                click.echo(click.style(f"  PASS", fg='green'))
            else:
                fail_count += 1
                click.echo(click.style(f"  FAIL", fg='red'))
                # Show which checks failed
                for check in result.get('checks', []):
                    if not check.get('pass', True):
                        click.echo(f"    {check['name']}: {check}")

        except Exception as e:
            result = {'config': desc, 'pass': False, 'error': str(e)}
            all_results.append(result)
            fail_count += 1
            click.echo(click.style(f"  FAIL: {e}", fg='red'))
        finally:
            try:
                os.unlink(wav_path)
            except OSError:
                pass
            # Ensure serve is dead
            try:
                serve_proc.kill()
            except Exception:
                pass

    # Summary
    click.echo(f"\n{'='*50}")
    total = pass_count + fail_count
    if fail_count == 0:
        click.echo(click.style(f"ALL PASS: {pass_count}/{total}", fg='green', bold=True))
    else:
        click.echo(click.style(f"FAIL: {fail_count}/{total} failed", fg='red', bold=True))

    if json_output:
        click.echo(json.dumps({
            'total': total, 'pass': pass_count, 'fail': fail_count,
            'results': all_results,
        }, indent=2))

    sys.exit(0 if fail_count == 0 else 1)


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
