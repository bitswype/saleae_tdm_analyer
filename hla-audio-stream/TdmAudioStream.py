import json
import struct
import socket
import sys
import threading
import collections

try:
    from saleae.analyzers import HighLevelAnalyzer, AnalyzerFrame, StringSetting, ChoicesSetting
except ImportError:
    # Running outside Logic 2 (e.g. test harness or self-test).
    # Provide minimal stubs so the module can be imported and tested.
    class HighLevelAnalyzer:  # type: ignore[no-redef]
        pass

    class AnalyzerFrame:  # type: ignore[no-redef]
        def __init__(self, frame_type=None, start_time=0, end_time=0, data=None):
            self.type = frame_type
            self.start_time = start_time
            self.end_time = end_time
            self.data = data or {}

    class StringSetting:  # type: ignore[no-redef]
        def __init__(self, **kw):
            pass

    class ChoicesSetting:  # type: ignore[no-redef]
        def __init__(self, choices, **kw):
            pass
        default = '16'

from _tdm_utils import parse_slot_spec, _as_signed, PerfCounters

PROTOCOL_VERSION = 1


class TdmAudioStream(HighLevelAnalyzer):
    """Logic 2 High Level Analyzer that streams selected TDM slots as live
    PCM audio over a TCP socket.

    A companion CLI tool (tdm-audio-bridge) connects to the TCP server,
    receives a JSON handshake with format metadata, then a continuous stream
    of raw interleaved PCM samples.

    Settings are injected by Logic 2 before __init__ is called.
    """

    # -------------------------------------------------------------------------
    # Settings — declared at class level so Logic 2 discovers and renders them.
    # -------------------------------------------------------------------------

    slots = StringSetting(label='Slots (e.g. 0,2,4 or 0-3)')
    tcp_port = StringSetting(label='TCP Port (default 4011)')
    buffer_size = StringSetting(label='Ring Buffer Size (frames, default 128)')
    bit_depth = ChoicesSetting(['16', '32'], label='Bit Depth')
    bit_depth.default = '16'

    # -------------------------------------------------------------------------
    # result_types — required by Logic 2 for frame label formatting.
    # -------------------------------------------------------------------------

    result_types = {
        'status': {'format': '{{data.message}}'},
        'error':  {'format': 'Error: {{data.message}}'},
    }

    def __init__(self):
        self._init_error = None

        try:
            self._perf = PerfCounters()

            self._slots_raw = self.slots
            self._bit_depth = int(self.bit_depth or '16')
            self._sign_mask = (1 << self._bit_depth) - 1
            self._sign_threshold = 1 << (self._bit_depth - 1)
            self._sign_subtract = 1 << self._bit_depth
            self._port = int(self.tcp_port or '4011')
            self._buf_size = int(self.buffer_size or '128')

            if self._port < 1024 or self._port > 65535:
                raise ValueError(
                    f"TCP port must be between 1024 and 65535, got {self._port}"
                )
            if self._buf_size < 1:
                raise ValueError(
                    f"Buffer size must be at least 1, got {self._buf_size}"
                )

            self._slot_list = parse_slot_spec(self._slots_raw)
            self._slot_set = set(self._slot_list)
            self._pcm_fmt = '<' + ('h' if self._bit_depth == 16 else 'i') * len(self._slot_list)

            self._frame_byte_size = len(self._slot_list) * (4 if self._bit_depth > 16 else 2)
            self._batch_size = max(1, self._buf_size // 2)
            self._batch_buf = bytearray(self._batch_size * self._frame_byte_size)
            self._batch_count = 0
            self._batch_offset = 0

            # Sample accumulator (same pattern as TdmWavExport)
            self._accum = {}
            self._last_frame_num = None
            self._timing_ref = {}
            self._sample_rate = None
            self._frame_count = 0

            # Ring buffer — deque drops oldest when full
            self._ring = collections.deque(maxlen=self._buf_size)
            self._ring_lock = threading.Lock()
            self._wake_event = threading.Event()

            # Client state
            self._client_lock = threading.Lock()
            self._current_client = None
            self._handshake_sent = False

            # Shutdown signal
            self._shutdown = threading.Event()

            # TCP server
            self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            if sys.platform == 'win32':
                self._server_sock.setsockopt(
                    socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1
                )
            else:
                self._server_sock.setsockopt(
                    socket.SOL_SOCKET, socket.SO_REUSEADDR, 1
                )
            self._server_sock.bind(('127.0.0.1', self._port))
            self._server_sock.listen(1)
            self._server_sock.settimeout(1.0)

            # Start background threads
            self._accept_thread = threading.Thread(
                target=self._accept_loop, daemon=True
            )
            self._accept_thread.start()

            self._sender_thread = threading.Thread(
                target=self._sender_loop, daemon=True
            )
            self._sender_thread.start()

        except Exception as e:
            self._init_error = str(e)
            # Safe defaults so decode() can run without AttributeError
            self._perf = PerfCounters()
            self._slot_list = []
            self._slot_set = set()
            self._pcm_fmt = '<'
            self._sign_mask = 0
            self._sign_threshold = 0
            self._sign_subtract = 0
            self._frame_byte_size = 0
            self._batch_size = 1
            self._batch_buf = bytearray()
            self._batch_count = 0
            self._batch_offset = 0
            self._accum = {}
            self._last_frame_num = None
            self._timing_ref = {}
            self._sample_rate = None
            self._frame_count = 0
            self._ring = collections.deque()
            self._ring_lock = threading.Lock()
            self._wake_event = threading.Event()
            self._client_lock = threading.Lock()
            self._current_client = None
            self._handshake_sent = False
            self._shutdown = threading.Event()

    # -------------------------------------------------------------------------
    # TCP server threads
    # -------------------------------------------------------------------------

    def _accept_loop(self):
        """Background thread: accept one client at a time."""
        while not self._shutdown.is_set():
            try:
                client, _addr = self._server_sock.accept()
                client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

                with self._client_lock:
                    # Disconnect previous client
                    if self._current_client is not None:
                        try:
                            self._current_client.close()
                        except OSError:
                            pass

                    self._current_client = client
                    self._handshake_sent = False

                    # Clear stale data from ring buffer
                    with self._ring_lock:
                        self._ring.clear()

                    # If sample rate already known, send handshake now
                    if self._sample_rate is not None:
                        self._send_handshake(client)

            except socket.timeout:
                continue
            except OSError:
                if self._shutdown.is_set():
                    break
                continue

    def _send_handshake(self, sock):
        """Send JSON handshake to client. Caller must hold _client_lock."""
        handshake = {
            'protocol': PROTOCOL_VERSION,
            'sample_rate': self._sample_rate,
            'channels': len(self._slot_list),
            'bit_depth': self._bit_depth,
            'slot_list': self._slot_list,
            'buffer_size': self._buf_size,
            'byte_order': 'little',
        }
        try:
            data = json.dumps(handshake, separators=(',', ':')).encode('utf-8') + b'\n'
            sock.sendall(data)
            self._handshake_sent = True
        except (OSError, BrokenPipeError):
            self._current_client = None
            self._handshake_sent = False

    def _sender_loop(self):
        """Background thread: drain ring buffer to connected client.

        Batches multiple frames into a single sendall() call to reduce
        per-frame overhead (lock acquisitions, syscalls, TCP packets).
        Without batching, stereo streams generate 44100+ individual 4-byte
        sends per second, which starves the decode thread of GIL time.
        """
        while not self._shutdown.is_set():
            self._wake_event.wait(timeout=0.1)
            self._wake_event.clear()

            while True:
                with self._ring_lock:
                    if not self._ring:
                        break
                    # Drain up to 1024 frames in one lock acquisition
                    batch = []
                    while self._ring and len(batch) < 1024:
                        batch.append(self._ring.popleft())

                if not batch:
                    break

                with self._client_lock:
                    client = self._current_client
                    if client is None:
                        continue

                try:
                    t0 = self._perf.begin('sender::send')
                    client.sendall(b''.join(batch))
                    self._perf.end('sender::send', t0)
                except (OSError, BrokenPipeError):
                    with self._client_lock:
                        self._current_client = None
                        self._handshake_sent = False

    # -------------------------------------------------------------------------
    # Sample accumulation and PCM packing
    # -------------------------------------------------------------------------

    def _try_derive_sample_rate(self, frame):
        """Derive audio sample rate from the interval between same-slot frames."""
        slot = frame.data['slot']
        if slot not in self._timing_ref:
            self._timing_ref[slot] = frame.start_time
            return
        if self._sample_rate is not None:
            return
        delta_sec = float(frame.start_time - self._timing_ref[slot])
        if delta_sec <= 0:
            return
        derived = round(1.0 / delta_sec)
        if derived < 1000 or derived > 200000:
            self._sample_rate = 48000  # sanity clamp fallback
        else:
            self._sample_rate = derived

    def _enqueue_frame(self):
        """Pack accumulated samples into the batch buffer.

        When the batch is full (batch_size frames), flush it to the ring
        buffer as a single chunk. This reduces lock acquisitions and
        struct.pack allocations from 48000/sec to ~750/sec for stereo 48kHz.
        """
        if not self._handshake_sent:
            with self._client_lock:
                if (self._current_client is not None
                        and not self._handshake_sent
                        and self._sample_rate is not None):
                    self._send_handshake(self._current_client)

        t0 = self._perf.begin('enqueue::pack')
        samples = [self._accum.get(slot, 0) for slot in self._slot_list]
        struct.pack_into(self._pcm_fmt, self._batch_buf, self._batch_offset, *samples)
        self._batch_offset += self._frame_byte_size
        self._batch_count += 1
        self._perf.end('enqueue::pack', t0)

        if self._batch_count >= self._batch_size:
            self._flush_batch()

        self._frame_count += 1

    def _flush_batch(self):
        """Flush the accumulated batch to the ring buffer."""
        if self._batch_count == 0:
            return
        t0 = self._perf.begin('enqueue::ring')
        chunk = bytes(self._batch_buf[:self._batch_offset])
        # CPython deque.append is atomic under the GIL; explicit lock kept
        # for safety but acquired once per batch, not per frame.
        with self._ring_lock:
            self._ring.append(chunk)
        self._wake_event.set()
        self._batch_offset = 0
        self._batch_count = 0
        self._perf.end('enqueue::ring', t0)

    def _try_flush(self, current_frame_num):
        """Detect TDM frame boundary and enqueue the completed accumulator.

        Flush BEFORE accumulating the new slot's data (flush-before-accumulate
        invariant) so the flush reads the previous frame's clean accumulator.
        """
        if self._last_frame_num is None:
            return
        if current_frame_num == self._last_frame_num:
            return
        # New TDM frame started — flush the completed accumulator
        if self._sample_rate is not None:
            self._enqueue_frame()
        self._accum = {}

    # -------------------------------------------------------------------------
    # decode() — called by Logic 2 for each upstream LLA frame
    # -------------------------------------------------------------------------

    def decode(self, frame):
        """Process one FrameV2 from the upstream TdmAnalyzer LLA.

        Returns None for normal frames, or AnalyzerFrame('error', ...) once
        if __init__ failed.
        """
        if self._init_error is not None:
            err_msg = self._init_error
            self._init_error = None
            return AnalyzerFrame('error', frame.start_time, frame.end_time,
                                 {'message': err_msg})

        if frame.type != 'slot':
            return None

        t0 = self._perf.begin('decode')

        d = frame.data
        slot = d['slot']
        frame_num = d['frame_number']

        if slot not in self._slot_set:
            self._perf.end('decode', t0)
            return None

        if self._sample_rate is None:
            self._try_derive_sample_rate(frame)

        t1 = self._perf.begin('decode::flush')
        self._try_flush(frame_num)
        self._perf.end('decode::flush', t1)

        if not (d.get('short_slot') or d.get('bitclock_error')):
            v = d.get('data', 0) & self._sign_mask
            if v >= self._sign_threshold:
                v -= self._sign_subtract
            self._accum[slot] = v

        self._last_frame_num = frame_num
        self._perf.end('decode', t0)
        return None

    def profile_summary(self):
        """Return profiling summary string. Only populated when TDM_HLA_PROFILE=1."""
        return self._perf.summary()

    def shutdown(self):
        """Cleanly stop TCP server and background threads."""
        self._flush_batch()
        self._shutdown.set()
        # Send stopping message to connected client
        with self._client_lock:
            if self._current_client is not None:
                try:
                    msg = json.dumps({'type': 'stopping', 'reason': 'analysis_ended'})
                    self._current_client.sendall(msg.encode('utf-8') + b'\n')
                except (OSError, BrokenPipeError):
                    pass
                try:
                    self._current_client.close()
                except OSError:
                    pass
                self._current_client = None
        try:
            self._server_sock.close()
        except OSError:
            pass


if __name__ == '__main__':
    import time

    # Self-test: verify the HLA can start, accept a connection, and stream PCM

    class _FakeFrame:
        """Minimal FrameV2 stand-in for self-testing."""
        def __init__(self, slot, frame_number, data_val, start_time=0.0):
            self.type = 'slot'
            self.start_time = start_time
            self.end_time = start_time
            self.data = {
                'slot': slot,
                'frame_number': frame_number,
                'data': data_val,
                'short_slot': False,
                'bitclock_error': False,
            }

    # Create HLA instance with settings injected
    hla = TdmAudioStream.__new__(TdmAudioStream)
    hla.slots = '0,1'
    hla.tcp_port = '14011'  # Use a high port to avoid conflicts
    hla.buffer_size = '256'
    hla.bit_depth = '16'
    hla.__init__()

    assert hla._init_error is None, f"Init failed: {hla._init_error}"
    assert hla._slot_list == [0, 1]
    assert hla._port == 14011

    # Feed warmup frames to derive sample rate (48kHz)
    sample_rate = 48000
    for frame_num in range(2):
        t = frame_num / sample_rate
        hla.decode(_FakeFrame(slot=0, frame_number=frame_num, data_val=0, start_time=t))
        hla.decode(_FakeFrame(slot=1, frame_number=frame_num, data_val=0, start_time=t))

    assert hla._sample_rate == sample_rate, f"Expected {sample_rate}, got {hla._sample_rate}"

    # Connect a TCP client
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.settimeout(5.0)
    client.connect(('127.0.0.1', 14011))

    # Read handshake
    buf = b''
    while b'\n' not in buf:
        buf += client.recv(4096)
    handshake_line, remainder = buf.split(b'\n', 1)
    handshake = json.loads(handshake_line)

    assert handshake['protocol'] == 1
    assert handshake['sample_rate'] == 48000
    assert handshake['channels'] == 2
    assert handshake['bit_depth'] == 16
    assert handshake['slot_list'] == [0, 1]

    # Feed test frames: slot0=100, slot1=200 for frame 2; slot0=300, slot1=400 for frame 3
    # Frame 4 triggers flush of frame 3
    test_data = [(100, 200), (300, 400)]
    for i, (s0, s1) in enumerate(test_data):
        fn = i + 2
        t = fn / sample_rate
        hla.decode(_FakeFrame(slot=0, frame_number=fn, data_val=s0, start_time=t))
        hla.decode(_FakeFrame(slot=1, frame_number=fn, data_val=s1, start_time=t))

    # Feed one more frame to trigger flush of the last test frame
    fn = len(test_data) + 2
    t = fn / sample_rate
    hla.decode(_FakeFrame(slot=0, frame_number=fn, data_val=999, start_time=t))

    # Flush any remaining batch buffer so data reaches the ring buffer
    hla._flush_batch()

    # Give sender thread time to transmit
    time.sleep(0.2)

    # Read PCM data — expect at least the test frames
    # First received frame might be warmup residual (frame 1's zeros)
    pcm = remainder
    try:
        while True:
            client.settimeout(0.5)
            chunk = client.recv(4096)
            if not chunk:
                break
            pcm += chunk
    except socket.timeout:
        pass

    client.close()

    # Each frame is 2 channels * 2 bytes = 4 bytes
    frame_size = 4
    n_frames = len(pcm) // frame_size
    received = []
    for i in range(n_frames):
        samples = struct.unpack('<2h', pcm[i * frame_size:(i + 1) * frame_size])
        received.append(samples)

    # We should have received frames. The first may be warmup zeros,
    # followed by our test data. Find our test data in the output.
    found_100_200 = False
    found_300_400 = False
    for s0, s1 in received:
        if s0 == 100 and s1 == 200:
            found_100_200 = True
        if s0 == 300 and s1 == 400:
            found_300_400 = True

    assert found_100_200, f"Test frame (100,200) not found in received: {received}"
    assert found_300_400, f"Test frame (300,400) not found in received: {received}"

    hla.shutdown()
    print("All self-tests passed.")
