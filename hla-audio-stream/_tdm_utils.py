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
