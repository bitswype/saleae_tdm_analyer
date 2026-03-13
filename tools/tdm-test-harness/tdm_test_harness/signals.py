"""Test signal generators for the TDM Audio Stream test harness.

All generators return a list of "frames" where each frame is a list of
integer sample values (one per channel), suitable for feeding to the
frame emitter.
"""

import math
import wave
import struct


def sine_wave(frequencies, sample_rate, channels, duration, bit_depth=16):
    """Generate a sine wave test signal.

    Args:
        frequencies: Single frequency (float/int) applied to all channels,
                     or a list of per-channel frequencies.
        sample_rate: Sample rate in Hz.
        channels: Number of output channels.
        duration: Duration in seconds.
        bit_depth: 16 or 32.

    Returns:
        List of frames, each frame is a list of `channels` signed integers.
    """
    max_val = (1 << (bit_depth - 1)) - 1
    n_frames = int(sample_rate * duration)

    if isinstance(frequencies, (int, float)):
        frequencies = [frequencies] * channels
    if len(frequencies) < channels:
        # Pad with the last frequency
        frequencies = list(frequencies) + [frequencies[-1]] * (channels - len(frequencies))

    frames = []
    for i in range(n_frames):
        t = i / sample_rate
        frame = []
        for ch in range(channels):
            val = int(max_val * math.sin(2 * math.pi * frequencies[ch] * t))
            frame.append(val)
        frames.append(frame)
    return frames


def silence(sample_rate, channels, duration, bit_depth=16):
    """Generate silence (all zeros).

    Returns:
        List of frames, each frame is a list of `channels` zeros.
    """
    n_frames = int(sample_rate * duration)
    return [[0] * channels for _ in range(n_frames)]


def ramp(sample_rate, channels, duration, bit_depth=16):
    """Generate a linear ramp from -max to +max over the duration.

    All channels receive the same ramp value.

    Returns:
        List of frames, each frame is a list of `channels` signed integers.
    """
    max_val = (1 << (bit_depth - 1)) - 1
    n_frames = int(sample_rate * duration)
    frames = []
    for i in range(n_frames):
        frac = i / max(n_frames - 1, 1)  # 0.0 to 1.0
        val = int(-max_val + frac * 2 * max_val)
        val = max(-max_val - 1, min(max_val, val))
        frames.append([val] * channels)
    return frames


def from_wav(path, target_bit_depth=16):
    """Read a WAV file and return frames plus metadata.

    Args:
        path: Path to the WAV file.
        target_bit_depth: Resample values to this bit depth (16 or 32).

    Returns:
        Tuple of (frames, sample_rate, channels) where frames is a list
        of lists of signed integers.
    """
    with wave.open(path, 'rb') as wf:
        sample_rate = wf.getframerate()
        channels = wf.getnchannels()
        n_frames = wf.getnframes()
        sample_width = wf.getsampwidth()
        raw = wf.readframes(n_frames)

    if sample_width == 1:
        # 8-bit WAV is unsigned, convert to signed
        samples = [b - 128 for b in raw]
        src_bits = 8
    elif sample_width == 2:
        samples = list(struct.unpack(f'<{n_frames * channels}h', raw))
        src_bits = 16
    elif sample_width == 4:
        samples = list(struct.unpack(f'<{n_frames * channels}i', raw))
        src_bits = 32
    else:
        raise ValueError(f"Unsupported WAV sample width: {sample_width} bytes")

    # Rescale to target bit depth if needed
    if src_bits != target_bit_depth:
        src_max = (1 << (src_bits - 1)) - 1
        dst_max = (1 << (target_bit_depth - 1)) - 1
        samples = [int(s * dst_max / src_max) for s in samples]

    # Group into frames
    frames = []
    for i in range(0, len(samples), channels):
        frames.append(samples[i:i + channels])

    return frames, sample_rate, channels


def parse_signal_spec(spec, sample_rate, channels, duration, bit_depth=16):
    """Parse a signal specification string and generate the signal.

    Formats:
        "sine:440"           — sine wave at 440 Hz on all channels
        "sine:440,880"       — 440 Hz on ch0, 880 Hz on ch1, etc.
        "silence"            — all zeros
        "ramp"               — linear ramp from -max to +max

    Returns:
        List of frames.
    """
    if spec.startswith('sine:'):
        freq_str = spec[5:]
        freqs = [float(f.strip()) for f in freq_str.split(',')]
        return sine_wave(freqs, sample_rate, channels, duration, bit_depth)
    elif spec == 'silence':
        return silence(sample_rate, channels, duration, bit_depth)
    elif spec == 'ramp':
        return ramp(sample_rate, channels, duration, bit_depth)
    else:
        raise ValueError(
            f"Unknown signal spec: {spec!r}. "
            f"Use sine:<freq>, sine:<f1>,<f2>,..., silence, or ramp"
        )


if __name__ == '__main__':
    # Self-tests
    frames = sine_wave(440, 48000, 2, 0.01, 16)
    assert len(frames) == 480, f"Expected 480 frames, got {len(frames)}"
    assert len(frames[0]) == 2, f"Expected 2 channels, got {len(frames[0])}"
    assert frames[0] == [0, 0], "Sine at t=0 should be zero"

    frames = silence(48000, 2, 0.01)
    assert all(f == [0, 0] for f in frames), "Silence should be all zeros"

    frames = ramp(48000, 1, 0.01, 16)
    assert frames[0][0] < 0, "Ramp should start negative"
    assert frames[-1][0] > 0, "Ramp should end positive"

    frames = parse_signal_spec('sine:440,880', 48000, 2, 0.01, 16)
    assert len(frames[0]) == 2

    frames = parse_signal_spec('silence', 48000, 4, 0.01)
    assert len(frames[0]) == 4

    print("All signal self-tests passed.")
