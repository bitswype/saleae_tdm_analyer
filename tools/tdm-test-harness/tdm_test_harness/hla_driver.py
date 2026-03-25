"""Drives the TdmAudioStream HLA outside of Logic 2.

Instantiates the HLA with injected settings (the way Logic 2 would) and
provides methods to feed fake frames and manage the lifecycle.
"""

import sys
import os

# Add hla-audio-stream to sys.path so we can import TdmAudioStream
_hla_dir = os.path.join(os.path.dirname(__file__), '..', '..', '..', 'hla-audio-stream')
_hla_dir = os.path.abspath(_hla_dir)
if _hla_dir not in sys.path:
    sys.path.insert(0, _hla_dir)

from TdmAudioStream import TdmAudioStream


class HlaDriver:
    """Instantiates and drives TdmAudioStream outside of Logic 2.

    Injects settings the way Logic 2 would (setting class attributes before
    calling __init__), then provides methods to feed frames and manage
    the TCP server lifecycle.
    """

    def __init__(self, slot_spec, port=4011, buffer_size=128, bit_depth=16):
        """Create and initialize the HLA.

        Args:
            slot_spec: Slot specification string (e.g. "0,1" or "0-3").
            port: TCP port number.
            buffer_size: Ring buffer size in frames.
            bit_depth: 16 or 32.
        """
        hla = TdmAudioStream.__new__(TdmAudioStream)
        # Inject settings as Logic 2 would
        hla.slots = slot_spec
        hla.tcp_port = str(port)
        hla.buffer_size = str(buffer_size)
        hla.bit_depth = str(bit_depth)
        hla.__init__()

        if hla._init_error is not None:
            raise RuntimeError(f"HLA init failed: {hla._init_error}")

        self._hla = hla

    def feed(self, frames):
        """Feed a sequence of frames to the HLA's decode method.

        Flushes any partial batch buffer after all frames have been fed
        so that data reaches the ring buffer (and TCP clients) promptly.

        Args:
            frames: Iterable of AnalyzerFrame-like objects (e.g. FakeFrame).
        """
        for frame in frames:
            self._hla.decode(frame)
        self._hla._flush_batch()

    def feed_one(self, frame):
        """Feed a single frame to the HLA's decode method."""
        self._hla.decode(frame)

    @property
    def port(self):
        """The TCP port the HLA is listening on."""
        return self._hla._port

    @property
    def sample_rate(self):
        """The derived sample rate (None if not yet derived)."""
        return self._hla._sample_rate

    @property
    def frame_count(self):
        """Number of complete PCM frames enqueued."""
        return self._hla._frame_count

    @property
    def init_error(self):
        """Init error message, or None if init succeeded."""
        return self._hla._init_error

    def shutdown(self):
        """Cleanly stop the HLA's TCP server and background threads."""
        self._hla.shutdown()


if __name__ == '__main__':
    # Self-test: verify driver can create and shut down the HLA
    driver = HlaDriver('0,1', port=14012, buffer_size=64, bit_depth=16)
    assert driver.port == 14012
    assert driver.sample_rate is None
    assert driver.frame_count == 0
    driver.shutdown()
    print("All hla_driver self-tests passed.")
