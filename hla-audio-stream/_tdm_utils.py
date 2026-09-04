def parse_slot_spec(spec: str) -> list:
    """Parse a slot specification string into an ordered, deduplicated list of slot indices.

    Supports comma-separated integers and hyphenated ranges. Whitespace around
    tokens and range endpoints is ignored. Insertion order is preserved after
    deduplication (not sorted ascending) so that WAV channels appear in the order
    the user specified.

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


import os
import time

_PROFILE = os.environ.get('TDM_HLA_PROFILE', '') == '1'

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


def convert_sample(value: int, params: tuple) -> int:
    """Reference implementation of the sample rescale (see conversion_params).

    The hot paths inline this arithmetic; this function is the readable
    single source of truth that the C backends are checked against.

    Rounding is round-half-up, written without an intermediate add so the
    C ports cannot overflow on values near the top of the source range:
        floor((v + 2^(s-1)) / 2^s) == (v >> s) + ((v >> (s-1)) & 1)
    Positive full scale can round up past the output maximum, so the result
    is clamped; negative full scale cannot round below the minimum.
    """
    src_mask, src_sign_bit, src_modulus, shift, out_max = params
    v = value & src_mask
    if v & src_sign_bit:
        v -= src_modulus
    if shift > 0:
        v = (v >> shift) + ((v >> (shift - 1)) & 1)
        if v > out_max:
            v = out_max
    elif shift < 0:
        v <<= -shift
    return v


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


def widen_pcm(data: bytes, src_bytes: int, dst_bytes: int) -> bytes:
    """Repack little-endian PCM samples from src_bytes to dst_bytes each.

    Used in Audio Batch Mode to turn the LLA's 1-byte or 3-byte packed
    samples into int16 or int32 for the TCP protocol, which only carries
    those two widths. Widening zero-fills the low bytes (a left shift by
    8 * (dst_bytes - src_bytes), so full scale stays full scale). Narrowing
    keeps the high bytes (a truncating right shift); it only happens if a
    batch claims a different width than the first one did, which a real
    LLA never does, but the stream must stay aligned regardless.
    Extended-slice assignment keeps the copy in C rather than Python.
    """
    if src_bytes == dst_bytes:
        return bytes(data)
    n = len(data) // src_bytes
    end = n * src_bytes  # ignore a trailing partial sample
    out = bytearray(n * dst_bytes)
    if dst_bytes > src_bytes:
        pad = dst_bytes - src_bytes
        for i in range(src_bytes):
            out[pad + i::dst_bytes] = data[i:end:src_bytes]
    else:
        drop = src_bytes - dst_bytes
        for i in range(dst_bytes):
            out[i::dst_bytes] = data[drop + i:end:src_bytes]
    return bytes(out)


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


if __name__ == '__main__':
    # Self-test: parse_slot_spec behavior
    assert parse_slot_spec("0,2,4") == [0, 2, 4], "basic comma-separated"
    assert parse_slot_spec("0-3") == [0, 1, 2, 3], "basic range"
    assert parse_slot_spec("1,3-5,7") == [1, 3, 4, 5, 7], "mixed comma and range"
    assert parse_slot_spec("2,2,2") == [2], "deduplication"
    assert parse_slot_spec("4,2,0") == [4, 2, 0], "insertion order preserved"
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
    assert _as_signed(0x7FFF, 16) == 32767, "max positive 16-bit"
    assert _as_signed(0, 16) == 0, "zero is zero"
    assert _as_signed(0x80000000, 32) == -2147483648, "MSB set = negative (32-bit)"
    assert _as_signed(65535, 16) == -1, "unsigned max = signed -1 (16-bit)"

    print("All self-tests passed.")
