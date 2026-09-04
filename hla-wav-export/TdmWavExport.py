import wave
import struct
import os
import time

_PROFILE = os.environ.get('TDM_HLA_PROFILE', '') == '1'


class PerfCounters:
    """Accumulates call counts and nanosecond totals per named section.

    Zero overhead when TDM_HLA_PROFILE env var is not set to '1'.
    Usage:
        _perf = PerfCounters()
        t = _perf.begin('section')
        ... work ...
        _perf.end('section', t)
    """
    def __init__(self):
        self._data = {}

    def begin(self, name):
        if not _PROFILE:
            return 0
        return time.perf_counter_ns()

    def end(self, name, start):
        if not _PROFILE:
            return
        elapsed = time.perf_counter_ns() - start
        entry = self._data.get(name)
        if entry is None:
            self._data[name] = [1, elapsed]
        else:
            entry[0] += 1
            entry[1] += elapsed

    def summary(self):
        if not self._data:
            return ''
        lines = [f"  {'Section':<30} {'Calls':>10} {'Total(ms)':>12} {'Per-call(us)':>14}"]
        for name, (count, total_ns) in self._data.items():
            if count == 0:
                continue
            lines.append(f"  {name:<30} {count:>10} {total_ns/1e6:>12.1f} {total_ns/count/1e3:>14.3f}")
        return '\n'.join(lines)

    def reset(self):
        self._data.clear()


try:
    from saleae.analyzers import HighLevelAnalyzer, AnalyzerFrame, StringSetting, ChoicesSetting
except ImportError:
    # Running outside Logic 2's embedded Python (e.g. self-test via python3).
    # Provide minimal stubs so module-level helpers can be imported and tested.
    class HighLevelAnalyzer:  # type: ignore[no-redef]
        pass
    class AnalyzerFrame:  # type: ignore[no-redef]
        pass
    class StringSetting:  # type: ignore[no-redef]
        def __init__(self, **kw): pass
    class ChoicesSetting:  # type: ignore[no-redef]
        def __init__(self, choices, **kw): pass
        default = '16'


def parse_slot_spec(spec: str) -> list:
    """Parse a slot specification string into an ordered, deduplicated list of slot indices.

    Supports comma-separated integers and hyphenated ranges. Whitespace around
    tokens and range endpoints is ignored. Insertion order is preserved after
    deduplication (not sorted ascending) so that WAV channels appear in the order
    the user specified (REQ-09).

    Examples:
        parse_slot_spec("0,2,4")  -> [0, 2, 4]
        parse_slot_spec("0-3")    -> [0, 1, 2, 3]
        parse_slot_spec("4,2,0")  -> [4, 2, 0]  (insertion order preserved)
        parse_slot_spec("2,2,2")  -> [2]          (deduplicated)

    Raises:
        ValueError: If a range is invalid (start > end) or no slots are parsed.
    """
    slots = []
    for token in spec.split(','):
        token = token.strip()
        if not token:
            continue
        if '-' in token:
            a, b = token.split('-', 1)
            a_int, b_int = int(a.strip()), int(b.strip())
            if a_int > b_int:
                raise ValueError(f"Invalid range: {token!r}")
            slots.extend(range(a_int, b_int + 1))
        else:
            slots.append(int(token))
    if not slots:
        raise ValueError(f"No slots parsed from: {spec!r}")
    # Preserve insertion order while deduplicating (dict.fromkeys preserves order)
    return list(dict.fromkeys(slots))


def _as_signed(value: int, bit_depth: int) -> int:
    """Convert an unsigned-style integer to a signed integer for the given bit depth.

    Masks the value to bit_depth bits, then applies two's complement sign
    conversion if the MSB is set. This is a no-op for values already in the
    signed range, making it safe to apply unconditionally regardless of whether
    the LLA has already sign-adjusted the sample.

    Examples:
        _as_signed(0x8000, 16) -> -32768   (MSB set = negative)
        _as_signed(0x7FFF, 16) -> 32767    (max positive, no-op)
        _as_signed(65535, 16)  -> -1       (unsigned max = signed -1)
    """
    mask = (1 << bit_depth) - 1
    value = value & mask
    if value >= (1 << (bit_depth - 1)):
        value -= (1 << bit_depth)
    return value


# The three helpers below are duplicated from hla-audio-stream/_tdm_utils.py.
# Logic 2 loads each extension folder in isolation, so the WAV export cannot
# import from the audio stream folder. Keep them in sync.

def parse_source_bit_depth(raw):
    """Parse the 'Source bit depth' HLA setting.

    Blank (or None) means auto-detect from the LLA's one-time 'format'
    frame, so returns None. Otherwise the value must be the LLA's
    'Data bits/slot' setting, 2-64.

    Raises:
        ValueError: with an actionable message for anything else.
    """
    # Outside Logic 2 (self-tests, harness) the attribute may still be the
    # class-level Setting object rather than an injected string: treat as unset
    if raw is None or not isinstance(raw, (str, int)):
        return None
    text = str(raw).strip()
    if not text:
        return None
    try:
        bits = int(text)
    except ValueError:
        bits = -1
    if not 2 <= bits <= 64:
        raise ValueError(
            f"Source bit depth must be between 2 and 64 (the LLA's 'Data bits/slot' "
            f"setting), got {text!r}. Leave it blank to auto-detect from the LLA."
        )
    return bits


def conversion_params(source_bits: int, output_bits: int) -> tuple:
    """Pre-compute the constants for rescaling LLA samples to the output width.

    The LLA emits each slot's 'data' as an integer at its own data width
    (mDataBitsPerSlot), already sign-converted when the LLA is in Signed
    mode. The HLA must interpret that integer at the SOURCE width and then
    rescale it to the OUTPUT width (16 or 32). Masking at the output width
    instead discards the upper bits of wider sources (GitHub issue #10).

    Returns (src_mask, src_sign_bit, src_modulus, shift, out_max) where
    shift > 0 means a rounded right shift by `shift` bits (source wider than
    output), shift < 0 means a left shift by -shift bits, and out_max is the
    clamp ceiling for the output range.
    """
    if not 2 <= source_bits <= 64:
        raise ValueError(f"source_bits must be 2-64, got {source_bits}")
    src_mask = (1 << source_bits) - 1
    src_sign_bit = 1 << (source_bits - 1)
    src_modulus = 1 << source_bits
    shift = source_bits - output_bits
    out_max = (1 << (output_bits - 1)) - 1
    return src_mask, src_sign_bit, src_modulus, shift, out_max


def lla_batch_bytes_per_sample(bit_depth: int) -> int:
    """Bytes per packed sample in an LLA 'audio_batch' frame.

    Mirrors TdmAnalyzer::WorkerThread: 1, 2, 3, or 4 bytes for data widths
    up to 8, 16, 24, and anything wider. Do not compute (bits + 7) // 8;
    a 40-bit LLA packs 4 bytes, not 5.
    """
    if bit_depth <= 8:
        return 1
    if bit_depth <= 16:
        return 2
    if bit_depth <= 24:
        return 3
    return 4


class TdmWavExport(HighLevelAnalyzer):
    """Logic 2 High Level Analyzer that exports selected TDM slots to a WAV file.

    Reads decoded frames from the TdmAnalyzer LLA and writes the selected slot
    samples to a standard WAV file using the Python stdlib `wave` module.

    Settings are injected by Logic 2 before __init__ is called.
    """

    # -------------------------------------------------------------------------
    # Settings — declared at class level so Logic 2 discovers and renders them
    # in the HLA settings panel before __init__ runs. Logic 2 injects values
    # as instance attributes; do NOT access self.<setting> in the class body.
    # -------------------------------------------------------------------------

    slots = StringSetting(label='Slots to export (required, e.g. 0,1 or 0-3 or 0,2,4-7)')
    output_path = StringSetting(label='Output WAV path (e.g. C:\\capture.wav or /tmp/capture.wav)')
    bit_depth = ChoicesSetting(['16', '32'],
        label='Output bit depth (ignored in Audio Batch Mode - LLA bit depth is used)')
    bit_depth.default = '16'  # Must be a separate statement — no default= kwarg allowed
    source_bit_depth = StringSetting(
        label='Source bit depth (LLA data bits/slot, 2-64). Leave blank to auto-detect from the LLA.')

    # -------------------------------------------------------------------------
    # result_types — required by Logic 2 for frame label formatting in the UI.
    # Double-brace syntax: {{data.field}} references frame.data['field'].
    # -------------------------------------------------------------------------

    result_types = {
        'status': {'format': '{{data.message}}'},
        'error':  {'format': 'Error: {{data.message}}'}
    }

    def __init__(self):
        # _init_error must be set first so decode() can always check it safely,
        # even if an exception occurs partway through the try block below.
        self._init_error = None

        try:
            self._perf = PerfCounters()

            # Capture injected setting values as private instance attributes.
            # Logic 2 has already set self.slots, self.output_path, self.bit_depth
            # as strings by the time __init__ is called.
            self._slots_raw = self.slots          # str — e.g. "0,2,4" or "0-3"
            self._output_path = self.output_path  # str — absolute path to .wav file
            # Convert bit depth to int; fallback to 16 handles the edge case where
            # the default attribute was not applied by an older Logic 2 build.
            self._bit_depth = int(self.bit_depth or '16')

            # Source bit depth is the LLA's data width. An explicit setting
            # wins; blank means auto-detect from the LLA's one-time 'format'
            # frame, falling back to the output width (pre-v2.6.0 behavior)
            # if that frame never arrives (older LLA build).
            self._source_bit_depth_setting = parse_source_bit_depth(
                getattr(self, 'source_bit_depth', ''))
            self._configure_conversion(self._source_bit_depth_setting or self._bit_depth)

            # REQ-16: Validate output_path before proceeding. Raising here is caught
            # by the except block below and stored as a deferred error.
            if not self.output_path or not self.output_path.strip():
                raise ValueError(
                    "output_path is required. Enter an absolute path, e.g. /home/user/capture.wav"
                )
            if not os.path.isabs(self.output_path.strip()):
                raise ValueError(
                    f"output_path must be an absolute path. Got: {self.output_path!r}"
                )

            # Parsed slot list — ordered per user specification (REQ-09)
            self._slot_list = parse_slot_spec(self._slots_raw)
            self._slot_set = set(self._slot_list)  # O(1) membership test (REQ-10)

            # Pre-compute PCM format string
            self._pcm_fmt = '<' + ('h' if self._bit_depth == 16 else 'i') * len(self._slot_list)

            # Batch buffer for WAV writes
            self._frame_byte_size = len(self._slot_list) * (4 if self._bit_depth > 16 else 2)
            self._batch_size = max(1, 64)  # batch 64 frames before writing to WAV
            self._batch_buf = bytearray(self._batch_size * self._frame_byte_size)
            self._batch_count = 0
            self._batch_offset = 0

            # WAV file state — lazy-opened on first frame (REQ-12)
            self._wav = None          # wave.Wave_write object, None until first frame
            self._sample_rate = None  # derived from frame timing (REQ-13)

            # Sample accumulator: dict[slot_index -> sample_value] for current TDM frame
            self._accum = {}
            self._last_frame_num = None  # track frame_number to detect TDM frame boundaries

            # Timing helper: stores first start_time seen per slot for rate derivation
            self._timing_ref = {}   # dict[slot -> first start_time seen]

            # Count of complete TDM frames written to WAV
            self._frame_count = 0

        except Exception as e:
            self._init_error = str(e)
            # Safe defaults so decode() can run without AttributeError
            self._perf = PerfCounters()
            self._slot_list = []
            self._slot_set = set()
            self._pcm_fmt = '<'
            self._source_bit_depth_setting = None
            self._src_bits = 16
            self._src_mask = 0
            self._src_sign_bit = 0
            self._src_modulus = 0
            self._shift = 0
            self._out_max = 0
            self._frame_byte_size = 0
            self._batch_size = 1
            self._batch_buf = bytearray()
            self._batch_count = 0
            self._batch_offset = 0
            self._wav = None
            self._sample_rate = None
            self._accum = {}
            self._last_frame_num = None
            self._timing_ref = {}
            self._frame_count = 0

    def _configure_conversion(self, src_bits: int) -> None:
        """Set the sample rescale constants for a given LLA data width."""
        (self._src_mask, self._src_sign_bit, self._src_modulus,
         self._shift, self._out_max) = conversion_params(src_bits, self._bit_depth)
        self._src_bits = src_bits

    def _apply_format_frame(self, d) -> None:
        """Adopt the LLA's data width from its one-time 'format' frame.

        An explicit 'Source bit depth' setting always wins. Otherwise the
        frame's bit_depth replaces the default assumption that the source
        width equals the output width.
        """
        if self._source_bit_depth_setting is not None:
            return
        bits = d.get('bit_depth')
        if not isinstance(bits, int) or not 2 <= bits <= 64 or bits == self._src_bits:
            return
        self._configure_conversion(bits)

    def _open_wav(self, sample_rate: int) -> None:
        """Open the output WAV file and configure it for streaming write.

        Must be called exactly once, after sample_rate is known.
        Do NOT use as a context manager — the file must stay open across
        many decode() calls.
        """
        n_channels = len(self._slot_list)
        sample_width = self._bit_depth // 8  # 2 for 16-bit, 4 for 32-bit
        self._wav = wave.open(self._output_path, 'wb')
        self._wav.setnchannels(n_channels)
        self._wav.setsampwidth(sample_width)
        self._wav.setframerate(sample_rate)

    def _try_derive_sample_rate(self, frame) -> None:
        """Attempt to derive the audio sample rate from frame timing.

        On the first occurrence of a given slot, stores the start_time as a
        reference. On the second occurrence, computes the TDM frame period and
        derives the sample rate. Subsequent calls are no-ops once the rate is
        known. Applies a sanity clamp (1000–200000 Hz) and falls back to 48000
        if the derived value is outside that range.
        """
        slot = frame.data['slot']
        if slot not in self._timing_ref:
            self._timing_ref[slot] = frame.start_time
            return
        if self._sample_rate is not None:
            return  # already derived
        delta_sec = float(frame.start_time - self._timing_ref[slot])
        if delta_sec <= 0:
            return  # same timestamp — skip to avoid division by zero
        derived = round(1.0 / delta_sec)
        if derived < 1000 or derived > 200000:
            self._sample_rate = 48000  # sanity clamp fallback
        else:
            self._sample_rate = derived

    def _write_wav_frame(self) -> None:
        """Pack the accumulated samples into the batch buffer.

        When the batch is full (batch_size frames), flush it to the WAV file
        as a single writeframes call. This reduces struct.pack allocations and
        WAV header patches from once-per-frame to once-per-batch.
        """
        t0 = self._perf.begin('wav::pack')
        samples = [self._accum.get(slot, 0) for slot in self._slot_list]
        struct.pack_into(self._pcm_fmt, self._batch_buf, self._batch_offset, *samples)
        self._batch_offset += self._frame_byte_size
        self._batch_count += 1
        self._perf.end('wav::pack', t0)

        if self._batch_count >= self._batch_size:
            self._flush_wav_batch()

        self._frame_count += 1

    def _flush_wav_batch(self) -> None:
        """Flush the accumulated batch to the WAV file."""
        if self._batch_count == 0:
            return
        t0 = self._perf.begin('wav::write')
        self._wav.writeframes(bytes(self._batch_buf[:self._batch_offset]))
        self._batch_offset = 0
        self._batch_count = 0
        self._perf.end('wav::write', t0)

    def _try_flush(self, current_frame_num: int) -> None:
        """Detect TDM frame boundaries and flush the completed accumulator.

        Called once per slot frame. When the frame_number changes, the previous
        TDM frame is complete and ready to write. Opens the WAV file on the first
        flush if sample_rate is available (lazy open per REQ-12).
        """
        if self._last_frame_num is None:
            return  # first frame ever — nothing to flush
        if current_frame_num == self._last_frame_num:
            return  # same TDM frame — still accumulating
        # A new TDM frame has started — flush the completed accumulator
        if self._sample_rate is not None and self._wav is None:
            self._open_wav(self._sample_rate)
        if self._wav is not None:
            self._write_wav_frame()
        self._accum = {}

    def profile_summary(self):
        """Return profiling summary string. Only populated when TDM_HLA_PROFILE=1."""
        return self._perf.summary()

    def _decode_audio_batch(self, frame):
        """Process a batched audio frame from the LLA and write to WAV."""
        d = frame.data
        pcm_data = d.get('pcm_data', b'')
        num_frames = d.get('num_frames', 0)
        lla_channels = d.get('channels', 0)
        bit_depth = d.get('bit_depth', 16)
        sample_rate = d.get('sample_rate', 0)

        if not pcm_data or num_frames == 0:
            return None

        # Set sample rate and bit depth from LLA batch metadata. The WAV
        # sample width must match the PACKED width (1/2/3/4 bytes), which
        # the LLA derives from its data width in tiers. Using the raw bit
        # count instead gave a 2-byte header over 3-byte data for a 20-bit
        # LLA, and wave.Error for 40-bit. Overrides the HLA's own setting.
        if self._sample_rate is None and sample_rate > 0:
            self._sample_rate = sample_rate
        if bit_depth > 0:
            self._bit_depth = lla_batch_bytes_per_sample(bit_depth) * 8

        # Open WAV file if not yet opened
        if self._wav is None and self._sample_rate is not None:
            self._open_wav(self._sample_rate)
        if self._wav is None:
            return None

        bytes_per_sample = lla_batch_bytes_per_sample(bit_depth)
        lla_frame_size = lla_channels * bytes_per_sample
        hla_channels = len(self._slot_list)

        # Fast path: all channels in natural order
        if (hla_channels == lla_channels
                and self._slot_list == list(range(lla_channels))):
            wav_data = bytes(pcm_data)
        else:
            # Extract selected slots from interleaved blob
            hla_frame_size = hla_channels * bytes_per_sample
            out = bytearray(num_frames * hla_frame_size)
            for f in range(num_frames):
                src_base = f * lla_frame_size
                dst_base = f * hla_frame_size
                for ch_idx, slot in enumerate(self._slot_list):
                    if slot < lla_channels:
                        src_off = src_base + slot * bytes_per_sample
                        dst_off = dst_base + ch_idx * bytes_per_sample
                        out[dst_off:dst_off + bytes_per_sample] = \
                            pcm_data[src_off:src_off + bytes_per_sample]
            wav_data = bytes(out)

        # 8-bit WAV is unsigned by specification; the LLA packs signed bytes
        if bytes_per_sample == 1:
            wav_data = bytes(b ^ 0x80 for b in wav_data)

        self._wav.writeframes(wav_data)
        self._frame_count += num_frames
        return None

    def decode(self, frame: AnalyzerFrame):
        """Process one FrameV2 from the upstream TdmAnalyzer LLA.

        Deferred error pattern (REQ-16, REQ-17): if __init__ caught an exception
        (e.g. invalid slots spec, missing or relative output_path), _init_error
        is set. The first decode() call emits one AnalyzerFrame('error', ...) with
        the error message visible in the Logic 2 protocol table, then clears
        _init_error so subsequent frames are silently ignored. This ensures the
        user sees a readable error rather than a silent crash.

        Advisory frames (frame.type == 'advisory') carry diagnostic messages
        and do not contain sample data — skip them to avoid KeyError on
        frame.data['slot']. All other frames are 'slot' frames.

        Returns None for normal frames, or AnalyzerFrame('error', ...) once if
        init failed.
        """
        # REQ-16/REQ-17: Emit deferred __init__ error as a visible protocol-table entry.
        # Cleared after first emission so subsequent frames are silently dropped.
        if self._init_error is not None:
            err_msg = self._init_error
            self._init_error = None  # clear - emit only once, then silence
            return AnalyzerFrame('error', frame.start_time, frame.end_time,
                                 {'message': err_msg})

        # Audio batch mode: LLA has pre-packed PCM, write directly to WAV
        if frame.type == 'audio_batch':
            return self._decode_audio_batch(frame)

        # One-time stream format from the LLA (v2.6.0+): learn the source
        # data width unless the user pinned it in settings
        if frame.type == 'format':
            self._apply_format_frame(frame.data)
            return None

        if frame.type != 'slot':
            return None

        t0 = self._perf.begin('decode')

        d = frame.data
        slot = d['slot']
        frame_num = d['frame_number']

        # Skip slots not in the user-specified filter set (REQ-10)
        if slot not in self._slot_set:
            self._perf.end('decode', t0)
            return None

        # Derive sample rate from frame timing (REQ-13)
        if self._sample_rate is None:
            self._try_derive_sample_rate(frame)

        # Detect TDM frame boundary and flush completed frame (REQ-15).
        # Flush BEFORE accumulating so the flush reads the previous frame's
        # clean accumulator, not the current slot's newly-arrived data.
        t1 = self._perf.begin('decode::flush')
        self._try_flush(frame_num)
        self._perf.end('decode::flush', t1)

        # Accumulate sample AFTER flush — error frames contribute silence by
        # not writing to accum; self._accum.get(slot, 0) returns 0 for them.
        if not (d.get('short_slot') or d.get('bitclock_error')):
            # Interpret 'data' at the SOURCE width, then rescale to the
            # output width: rounded right shift (clamped) or left shift.
            # See conversion_params above and GitHub issue #10.
            v = d.get('data', 0) & self._src_mask
            if v & self._src_sign_bit:
                v -= self._src_modulus
            sh = self._shift
            if sh > 0:
                v = (v >> sh) + ((v >> (sh - 1)) & 1)
                if v > self._out_max:
                    v = self._out_max
            elif sh < 0:
                v <<= -sh
            self._accum[slot] = v

        # Update frame tracker
        self._last_frame_num = frame_num

        self._perf.end('decode', t0)
        return None

    def shutdown(self):
        """Flush remaining batch and close the WAV file."""
        self._flush_wav_batch()
        if self._wav is not None:
            try:
                self._wav.close()
            except Exception:
                pass
            self._wav = None


if __name__ == '__main__':
    # Self-test: parse_slot_spec behavior
    assert parse_slot_spec("0,2,4") == [0, 2, 4], "basic comma-separated"
    assert parse_slot_spec("0-3") == [0, 1, 2, 3], "basic range"
    assert parse_slot_spec("1,3-5,7") == [1, 3, 4, 5, 7], "mixed comma and range"
    assert parse_slot_spec("2,2,2") == [2], "deduplication"
    assert parse_slot_spec("4,2,0") == [4, 2, 0], "insertion order preserved (NOT sorted ascending)"
    assert parse_slot_spec("  0 , 2 ") == [0, 2], "whitespace tolerance"

    try:
        parse_slot_spec("5-2")
        assert False, "should have raised ValueError for invalid range"
    except ValueError as e:
        assert "Invalid range" in str(e), f"wrong error message: {e}"

    try:
        parse_slot_spec("")
        assert False, "should have raised ValueError for empty spec"
    except ValueError as e:
        assert "No slots parsed" in str(e), f"wrong error message: {e}"

    # Self-test: _as_signed behavior
    assert _as_signed(0x8000, 16) == -32768, "MSB set = negative (16-bit)"
    assert _as_signed(0x7FFF, 16) == 32767, "max positive 16-bit (no-op)"
    assert _as_signed(0, 16) == 0, "zero is zero"
    assert _as_signed(0x80000000, 32) == -2147483648, "MSB set = negative (32-bit)"
    assert _as_signed(65535, 16) == -1, "unsigned max = signed -1 (16-bit)"

    print("All self-tests passed.")

    # Self-test: decode() flush-before-accumulate ordering (REQ-15)
    import io, wave as _wave

    class _FakeFrame:
        """Minimal FrameV2 stand-in for self-testing decode() ordering."""
        type = 'slot'
        start_time = 0.0
        end_time   = 0.0
        def __init__(self, slot, frame_number, data_val):
            self.data = {
                'slot': slot,
                'frame_number': frame_number,
                'data': data_val,
                'short_slot': False,
                'bitclock_error': False,
            }

    import tempfile, os as _os
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tf:
        _tmp_path = tf.name
    try:
        # Patch settings as Logic 2 would inject them
        hla = TdmWavExport.__new__(TdmWavExport)
        hla.slots        = '0,1'
        hla.output_path  = _tmp_path
        hla.bit_depth    = '16'
        hla.__init__()

        # Provide an explicit sample rate so _open_wav fires on first flush
        hla._sample_rate = 48000

        # Feed: TDM frame 0 (slot0=100, slot1=200), TDM frame 1 (slot0=300, slot1=400)
        # A third TDM frame is needed to trigger the flush of frame 1.
        frames_in = [
            _FakeFrame(slot=0, frame_number=0, data_val=100),
            _FakeFrame(slot=1, frame_number=0, data_val=200),
            _FakeFrame(slot=0, frame_number=1, data_val=300),
            _FakeFrame(slot=1, frame_number=1, data_val=400),
            _FakeFrame(slot=0, frame_number=2, data_val=999),  # triggers flush of frame 1
        ]
        for f in frames_in:
            hla.decode(f)

        # Flush remaining batch and close WAV before reading back
        hla.shutdown()

        # Read back the WAV and verify sample values
        with _wave.open(_tmp_path, 'rb') as wf:
            raw = wf.readframes(wf.getnframes())
        samples = list(struct.unpack(f'<{len(raw)//2}h', raw))
        # Expect [100, 200, 300, 400] — frame 2 (999) not yet flushed
        assert samples == [100, 200, 300, 400], \
            f"REQ-15 ordering bug: expected [100, 200, 300, 400] got {samples}"
    finally:
        _os.unlink(_tmp_path)

    print("REQ-15 decode ordering test passed.")
