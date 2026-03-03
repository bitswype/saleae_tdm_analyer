import wave
import struct
import os
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

    slots = StringSetting(label='Slots (e.g. 0,2,4 or 0-3)')
    output_path = StringSetting(label='Output Path (absolute path to .wav file)')
    bit_depth = ChoicesSetting(['16', '32'], label='Bit Depth')
    bit_depth.default = '16'  # Must be a separate statement — no default= kwarg allowed

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
            # Capture injected setting values as private instance attributes.
            # Logic 2 has already set self.slots, self.output_path, self.bit_depth
            # as strings by the time __init__ is called.
            self._slots_raw = self.slots          # str — e.g. "0,2,4" or "0-3"
            self._output_path = self.output_path  # str — absolute path to .wav file
            # Convert bit depth to int; fallback to 16 handles the edge case where
            # the default attribute was not applied by an older Logic 2 build.
            self._bit_depth = int(self.bit_depth or '16')

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
            self._slot_list = []
            self._slot_set = set()
            self._wav = None
            self._sample_rate = None
            self._accum = {}
            self._last_frame_num = None
            self._timing_ref = {}
            self._frame_count = 0

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
        """Pack the accumulated samples for the current TDM frame and write to WAV.

        Interleaves samples in slot-list order. Missing slots (not received in
        this TDM frame) contribute silence (zero) per REQ-11. wave.writeframes()
        calls _patchheader() internally after every write, satisfying REQ-14.
        """
        fmt = '<' + ('h' if self._bit_depth == 16 else 'i') * len(self._slot_list)
        samples = [self._accum.get(slot, 0) for slot in self._slot_list]
        packed = struct.pack(fmt, *samples)
        self._wav.writeframes(packed)
        self._frame_count += 1

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
            self._init_error = None  # clear — emit only once, then silence
            return AnalyzerFrame('error', frame.start_time, frame.end_time,
                                 {'message': err_msg})

        if frame.type != 'slot':
            return None

        slot = frame.data['slot']
        frame_num = frame.data['frame_number']

        # Skip slots not in the user-specified filter set (REQ-10)
        if slot not in self._slot_set:
            return None

        # Accumulate sample — error frames contribute silence by not writing to
        # accum; self._accum.get(slot, 0) will return 0 for missing entries
        if not (frame.data.get('short_slot') or frame.data.get('bitclock_error')):
            # REQ-18/REQ-19: .get() guard — data field always present per LLA schema but defensive access costs nothing
            self._accum[slot] = _as_signed(frame.data.get('data', 0), self._bit_depth)

        # Derive sample rate from frame timing (REQ-13)
        self._try_derive_sample_rate(frame)

        # Detect TDM frame boundary and flush completed frame (REQ-15)
        self._try_flush(frame_num)

        # Update frame tracker
        self._last_frame_num = frame_num

        return None


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
