"""Audio playback engine using sounddevice.

Receives raw PCM bytes from the StreamClient and plays them through
the selected audio output device. Handles format conversion, buffering,
and device management.
"""

import threading
import logging

log = logging.getLogger('tdm-audio-bridge')

# Lazy imports — sounddevice and numpy require PortAudio at import time.
# Deferring lets the CLI show help text and run non-audio commands even
# when PortAudio is not installed.
sd = None
np = None


def _ensure_audio():
    """Import sounddevice and numpy, raising a clear error if unavailable."""
    global sd, np
    if sd is not None:
        return
    try:
        import sounddevice as _sd
        import numpy as _np
        sd = _sd
        np = _np
    except OSError as e:
        raise RuntimeError(
            f"Audio subsystem not available: {e}\n"
            "Install PortAudio (e.g. 'apt install libportaudio2' on Linux, "
            "or 'brew install portaudio' on macOS)."
        ) from e


def list_output_devices():
    """Return a list of available output devices.

    Each entry is a dict with 'index', 'name', and 'max_channels'.
    """
    _ensure_audio()
    devices = sd.query_devices()
    results = []
    for i, d in enumerate(devices):
        if d['max_output_channels'] > 0:
            results.append({
                'index': i,
                'name': d['name'],
                'max_channels': d['max_output_channels'],
            })
    return results


def find_device(name_or_index):
    """Find an output device by name substring or index.

    Returns the device index, or raises ValueError if not found.
    """
    _ensure_audio()
    if name_or_index is None:
        return None  # use default

    # Try as integer index first
    try:
        idx = int(name_or_index)
        info = sd.query_devices(idx)
        if info['max_output_channels'] > 0:
            return idx
        raise ValueError(f"Device {idx} has no output channels")
    except (ValueError, sd.PortAudioError):
        pass

    # Search by name substring (case-insensitive)
    needle = name_or_index.lower()
    for dev in list_output_devices():
        if needle in dev['name'].lower():
            return dev['index']

    raise ValueError(f"No output device matching '{name_or_index}'")


class Player:
    """Plays PCM audio received from the HLA stream.

    Uses sounddevice's OutputStream with a callback that pulls from an
    internal byte buffer. The buffer is fed by calling feed(data).
    """

    def __init__(self, handshake, device=None, latency='high',
                 prebuffer_ms=500):
        _ensure_audio()
        self._handshake = handshake
        self._device = device
        self._latency = latency
        self._stream = None

        # Pre-buffer: accumulate this many bytes before starting playback
        bytes_per_sample = handshake.bit_depth // 8
        prebuffer_frames = int(handshake.sample_rate * prebuffer_ms / 1000)
        self._prebuffer_bytes = prebuffer_frames * handshake.channels * bytes_per_sample
        self._started = False

        # Volume control (0.0 = silence, 1.0 = unity, >1.0 = gain)
        self._volume = 1.0
        self._muted = False

        # Internal buffer for received PCM.
        # Uses a read offset to avoid O(n) shift on every audio callback.
        self._lock = threading.Lock()
        self._buf = bytearray()
        self._read_pos = 0
        self._compact_threshold = 256 * 1024  # compact after 256 KiB consumed

        # Stats
        self._underruns = 0

    @property
    def underruns(self):
        return self._underruns

    @property
    def is_playing(self):
        return self._started

    @property
    def buffer_level(self):
        with self._lock:
            return (len(self._buf) - self._read_pos) / max(self._prebuffer_bytes, 1)

    @property
    def volume(self):
        return self._volume

    @volume.setter
    def volume(self, value):
        self._volume = max(0.0, min(float(value), 2.0))

    @property
    def muted(self):
        return self._muted

    @muted.setter
    def muted(self, value):
        self._muted = bool(value)

    def start(self):
        """Mark the player as ready to start.

        Actual playback begins once the pre-buffer threshold is reached,
        ensuring smooth initial playback without underruns.
        """
        self._started = False
        log.info("Buffering %d ms before playback...",
                 int(self._prebuffer_bytes * 1000 /
                     (self._handshake.sample_rate * self._handshake.channels *
                      (self._handshake.bit_depth // 8))))

    def _open_stream(self):
        """Actually open the sounddevice stream."""
        hs = self._handshake
        dtype = 'int16' if hs.bit_depth == 16 else 'int32'

        log.info("Opening audio: %d Hz, %d ch, %s, device=%s",
                 hs.sample_rate, hs.channels, dtype, self._device)

        self._stream = sd.OutputStream(
            samplerate=hs.sample_rate,
            channels=hs.channels,
            dtype=dtype,
            device=self._device,
            latency=self._latency,
            blocksize=1024,
            callback=self._audio_callback,
        )
        self._stream.start()
        self._started = True

    def stop(self):
        """Stop and close the audio stream."""
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            except sd.PortAudioError as e:
                log.warning("Error closing stream: %s", e)
            self._stream = None

    def feed(self, data: bytes):
        """Add raw PCM data to the playback buffer.

        If the stream hasn't started yet, begins playback once the
        pre-buffer threshold is reached.
        """
        with self._lock:
            self._buf.extend(data)
            buf_len = len(self._buf) - self._read_pos

        if not self._started and buf_len >= self._prebuffer_bytes:
            self._open_stream()

    def _audio_callback(self, outdata, frame_count, time_info, status):
        """sounddevice callback — fill output buffer from internal store."""
        if status:
            log.debug("Audio status: %s", status)

        hs = self._handshake
        bytes_per_sample = hs.bit_depth // 8
        needed = frame_count * hs.channels * bytes_per_sample

        with self._lock:
            available = len(self._buf) - self._read_pos
            if available >= needed:
                raw = bytes(
                    memoryview(self._buf)[self._read_pos:self._read_pos + needed])
                self._read_pos += needed
                # Compact periodically to reclaim consumed space
                if self._read_pos >= self._compact_threshold:
                    del self._buf[:self._read_pos]
                    self._read_pos = 0
            elif available > 0:
                # Partial — pad with silence
                raw = (bytes(memoryview(self._buf)[self._read_pos:])
                       + b'\x00' * (needed - available))
                self._buf.clear()
                self._read_pos = 0
                self._underruns += 1
            else:
                # No data — output silence
                outdata[:] = 0
                self._underruns += 1
                return

        # Convert bytes to numpy array for sounddevice
        dtype = np.int16 if hs.bit_depth == 16 else np.int32
        samples = np.frombuffer(raw, dtype=dtype).reshape(-1, hs.channels)

        if self._muted:
            outdata[:] = 0
        else:
            vol = self._volume
            if vol == 1.0:
                outdata[:len(samples)] = samples
            else:
                # Apply volume as float to avoid integer overflow, then clip
                max_val = 32767 if hs.bit_depth == 16 else 2147483647
                scaled = np.clip(samples.astype(np.float64) * vol,
                                 -max_val - 1, max_val).astype(dtype)
                outdata[:len(samples)] = scaled
            if len(samples) < frame_count:
                outdata[len(samples):] = 0
