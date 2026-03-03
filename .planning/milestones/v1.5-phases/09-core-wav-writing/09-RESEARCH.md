# Phase 9: Core WAV Writing - Research

**Researched:** 2026-03-02
**Domain:** Python `wave` module, slot parsing, multi-channel PCM packing, Logic 2 HLA frame timing
**Confidence:** HIGH

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| REQ-08 | HLA parses the `slots` setting and filters incoming frames to the specified slot numbers | Slot spec parser pattern documented — regex + range expansion into sorted list |
| REQ-09 | Slots are written to the WAV as channels in the order they were specified | Channel ordering = slot order in parsed list; `wave` writes interleaved samples per frame |
| REQ-10 | Unspecified slots are silently discarded | Filter: `if frame.data['slot'] not in self._slot_set: return None` |
| REQ-11 | If a specified slot is missing from a frame, silence (zero) is written for that channel | Frame accumulator dict; fill missing slots with 0 before flushing |
| REQ-12 | HLA opens the output WAV file when the first frame is received | Lazy-open pattern: `_wav` is None until first slot frame; open with `wave.open()` on first hit |
| REQ-13 | WAV is written as standard PCM (integer samples) at the sample rate derived from frame timing | Sample rate = 1 / float(frame1.start_time - frame0.start_time) across consecutive frames with same slot; confirmed via GraphTime subtraction |
| REQ-14 | WAV header is refreshed periodically (every ~1000 frames) so partial captures are playable | `_patchheader()` is called internally by `writeframes()`; trigger every N complete TDM frames via frame counter |
| REQ-15 | Each frame in the WAV corresponds to one TDM frame (one sample per channel per frame) | One call to `writeframes(packed_samples)` per complete TDM frame accumulation |
</phase_requirements>

## Summary

Phase 9 implements the full `decode()` method in `TdmWavExport.py`, adding slot parsing, WAV file lifecycle management, sample accumulation, and periodic header refresh. The Python `wave` module (stdlib 3.8) handles all WAV header bookkeeping and sample writing. The `_patchheader()` mechanism in `wave.Wave_write` performs the seekback header refresh automatically when `writeframes()` is called — the implementation does not need to seek manually.

The most critical design insight is that the TDM LLA emits one FrameV2 per slot per TDM frame, not one FrameV2 per complete TDM frame. The HLA must therefore accumulate samples across consecutive slot frames that share the same `frame_number`, then flush a complete interleaved frame to the WAV file once all expected slots have arrived (or the `frame_number` changes). The C++ plugin uses a slot-index-based accumulator array (`mSampleData[mSampleIndex++]`) — the same pattern applies in Python using a dict keyed by slot number.

The `data` field in FrameV2 is already sign-adjusted by the LLA when the LLA setting "Signed" is enabled; the HLA receives either a signed or unsigned integer depending on how the LLA is configured. For WAV PCM, 16-bit samples must be signed integers (`struct.pack('<h', value)`), and 32-bit samples must also be signed (`struct.pack('<i', value)`). The sample rate is derived from the first two consecutive frames with the same slot number by computing `1.0 / float(frame_b.start_time - frame_a.start_time)` — GraphTime subtraction yields a GraphTimeDelta that converts to float seconds.

**Primary recommendation:** Use `wave.open(path, 'wb')`, set params once (before first write), then call `wav.writeframes(struct.pack(...))` once per complete TDM frame. The `wave` module handles header patching internally on every `writeframes()` call. Trigger a periodic explicit `_patchheader()` call every 1000 frames using the public `writeframes()` with empty bytes if needed — but `writeframes()` already patches on every call, so the periodic refresh requirement is satisfied automatically.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `wave` | Python 3.8 stdlib | WAV file open, params, frame writing, header management | Only officially supported WAV writer in Logic 2's embedded Python |
| `struct` | Python 3.8 stdlib | Pack signed integer samples into bytes for `writeframes()` | Standard binary packing; no alternatives in stdlib |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `os.path` | Python 3.8 stdlib | Validate `output_path` is absolute before opening | `__init__` guard, not `decode()` |
| `re` | Python 3.8 stdlib | Parse range notation in slot spec (e.g. `0-3`) | Optional — plain string split is sufficient for most cases |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `wave` module | Manual binary file + `struct` | `wave` handles all header fields, chunk sizes, padding; manual writer is fragile and replicates work already done in C++ plugin |
| `struct.pack` per sample | `bytearray` + manual bit ops | `struct.pack` is the canonical WAV sample packing method and handles endianness correctly |
| Eager file open in `__init__` | Lazy open on first frame (REQ-12) | Lazy open avoids creating a zero-byte WAV file if no frames arrive |

**Installation:** No installation needed. Both `wave` and `struct` are Python 3.8 stdlib modules present in Logic 2's embedded Python runtime.

## Architecture Patterns

### Recommended State Layout in `__init__`

```python
def __init__(self):
    self._slots_raw  = self.slots
    self._output_path = self.output_path
    self._bit_depth  = int(self.bit_depth or '16')

    # Parsed slot list — ordered, e.g. [0, 2, 4]
    self._slot_list  = parse_slot_spec(self._slots_raw)
    self._slot_set   = set(self._slot_list)  # O(1) membership test

    # WAV file state (lazy-opened on first frame)
    self._wav        = None   # wave.Wave_write object
    self._sample_rate = None  # derived from first pair of same-slot frames

    # Sample accumulator: dict[slot_index -> sample_value] for current TDM frame
    self._accum      = {}
    self._last_frame_num = None  # track frame_number to detect frame boundaries

    # Timing helper: store first frame's start_time per slot for rate derivation
    self._timing_ref = {}  # dict[slot -> first start_time seen]
    self._prev_start = {}  # dict[slot -> previous start_time] for rate calc

    # Periodic refresh counter (tracks complete TDM frames written)
    self._frame_count = 0
```

### Pattern 1: Slot Spec Parser

**What:** Parse `"0,2,4"` or `"0-3"` or `"1,3-5,7"` into a sorted list of ints.
**When to use:** In `__init__` — raises `ValueError` for invalid specs (caught for REQ-17).

```python
# Source: standard Python range/split idiom — no library needed
def parse_slot_spec(spec: str) -> list:
    """Return sorted list of slot indices from spec like '0,2,4' or '0-3'."""
    slots = []
    for token in spec.split(','):
        token = token.strip()
        if not token:
            continue
        if '-' in token:
            parts = token.split('-', 1)
            start, end = int(parts[0].strip()), int(parts[1].strip())
            if start > end:
                raise ValueError(f"Invalid range: {token}")
            slots.extend(range(start, end + 1))
        else:
            slots.append(int(token))
    if not slots:
        raise ValueError("No slots specified")
    return sorted(set(slots))  # deduplicate and sort
```

**Verified behavior:** `"0,2,4"` → `[0, 2, 4]`, `"0-3"` → `[0, 1, 2, 3]`, `"1,3-5,7"` → `[1, 3, 4, 5, 7]`

### Pattern 2: Sample Rate Derivation from GraphTime

**What:** Derive TDM frame rate (audio sample rate) from the difference between consecutive `start_time` values of the same slot.
**When to use:** On the first two frames that share the same slot number.
**Source:** [Saleae forum — GraphTimeDelta](https://discuss.saleae.com/t/graphtimedelta-with-high-resolution/2763) + [Pulse duration example](https://discuss.saleae.com/t/how-to-get-the-duration-of-each-pulse-captured-for-ppm-analyzer/1414)

```python
# frame.start_time is a saleae.data.GraphTime object.
# Subtracting two GraphTime objects yields a GraphTimeDelta.
# Calling float() on a GraphTimeDelta returns the delta in seconds.
# Source: discuss.saleae.com/t/how-to-get-the-duration-of-each-pulse-captured-for-ppm-analyzer/1414

duration_seconds = float(frame_b.start_time - frame_a.start_time)
sample_rate = round(1.0 / duration_seconds)
```

**Important:** The duration between consecutive slot=0 frames equals one TDM frame period (1 / audio sample rate). Use the SAME slot for both measurements to avoid confusion with inter-slot timing.

**Fallback:** If fewer than 2 frames arrive before close (very short capture), default to 48000 Hz.

### Pattern 3: WAV File Lifecycle (Lazy Open)

**What:** Open the WAV file on the first valid slot frame; never in `__init__`.
**When to use:** REQ-12 — avoid creating files when no frames arrive.

```python
# Source: Python 3.8 docs — https://docs.python.org/3.8/library/wave.html
import wave
import struct

def _open_wav(self, sample_rate: int):
    """Open the output WAV file and write the header."""
    n_channels = len(self._slot_list)
    sample_width = self._bit_depth // 8  # bytes: 16-bit -> 2, 32-bit -> 4

    self._wav = wave.open(self._output_path, 'wb')
    self._wav.setnchannels(n_channels)
    self._wav.setsampwidth(sample_width)
    self._wav.setframerate(sample_rate)
    # Note: setnframes(0) is NOT needed — wave.writeframes() patches nframes automatically
```

**Critical:** Call `wave.open(..., 'wb')` not `open(..., 'wb')`. The `wave.open()` wrapper creates a `Wave_write` object that manages header writing.

### Pattern 4: Sample Accumulation and Frame Flush

**What:** Accumulate one sample per expected slot, flush when a complete TDM frame is ready.
**When to use:** The TDM LLA emits one FrameV2 per slot. The WAV must write one interleaved frame per TDM frame (REQ-15).

```python
# Source: C++ pattern from PCMWaveFileHandler::addSample() adapted to Python
# The C++ uses mSampleData[mSampleIndex++]; Python uses a dict accumulator.

def _try_flush(self, current_frame_num):
    """If frame_number changed, flush the completed accumulator to WAV."""
    if self._last_frame_num is None:
        self._last_frame_num = current_frame_num
        return

    if current_frame_num != self._last_frame_num:
        self._write_wav_frame()
        self._accum = {}
        self._last_frame_num = current_frame_num

def _write_wav_frame(self):
    """Pack accumulated samples (one per slot) and write to WAV file."""
    fmt = '<' + ('h' if self._bit_depth == 16 else 'i') * len(self._slot_list)
    samples = [self._accum.get(slot, 0) for slot in self._slot_list]  # 0 = silence for missing slot (REQ-11)
    packed = struct.pack(fmt, *samples)
    self._wav.writeframes(packed)
    self._frame_count += 1
```

**REQ-11 compliance:** `self._accum.get(slot, 0)` returns 0 (silence) for any slot not received in this TDM frame.

### Pattern 5: Periodic Header Refresh

**What:** `wave.writeframes()` patches the header (RIFF chunk size + data chunk size) automatically on every call via `_patchheader()` in CPython's `wave.py`.
**When to use:** No additional manual seekback needed — the `wave` module handles this.
**Source:** CPython 3.8 `wave.py` — `Wave_write.writeframes()` calls `_patchheader()` after every write.

```python
# CPython wave.py Wave_write.writeframes() (Python 3.8):
# def writeframes(self, data):
#     self._ensure_header_written(len(data))
#     nframes = len(data) // (self._sampwidth * self._nchannels)
#     if self._convert:
#         data = self._convert(data)
#     self._nframeswritten += nframes
#     self._datawritten += len(data)
#     self._file.write(data)
#     self._patchheader()  # <-- called after EVERY writeframes()
```

**Implication:** REQ-14 ("refresh every ~1000 frames") is satisfied automatically by calling `writeframes()` once per TDM frame. No manual seek counter needed. If the implementation batches writes (e.g., every 1000 frames), the patch still happens on each batch call. Either approach satisfies REQ-14.

### Anti-Patterns to Avoid

- **Opening WAV in `__init__`:** WAV will be created even if no frames arrive. Always open lazily on first frame (REQ-12).
- **Writing one sample per `writeframes()` call:** No functional problem, but calling `_patchheader()` (which does two `seek()` calls) on every single sample is unnecessary. Batch by TDM frame (one call per complete frame = one call per N channels).
- **Using `wave.open(path, 'w')` (no 'b'):** On Windows, text-mode opens break binary WAV writing. Always use `'wb'`.
- **Forgetting to call `wav.close()` on shutdown:** Logic 2 provides no destructor hook. The WAV header is patched on every `writeframes()` call, so the file is always valid after the last write — but `close()` is still needed to flush the OS buffer. Since no finalizer exists, the periodic `writeframes()` pattern IS the close strategy.
- **Using `writeframesraw()` instead of `writeframes()`:** `writeframesraw()` does NOT call `_patchheader()`. Always use `writeframes()` to ensure the header stays current.
- **Writing 32-bit samples as unsigned:** Standard PCM WAV 32-bit format uses signed integers. Pack with `struct.pack('<i', value)`, not `'<I'`. The C++ LLA stores data in `mData1` (U64) but adjusts sign via `ConvertToSignedNumber` if the LLA is in signed mode. Treat the incoming `data` field as potentially signed Python int and clamp to range.
- **Using `int` as format for 16-bit:** Use `'<h'` for signed 16-bit short. `'<i'` is 32-bit. `'<H'` is unsigned 16-bit (wrong for PCM).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV header writing | Custom RIFF header bytes | `wave.open()` + `wave.Wave_write` | Header has 6 fields, chunk nesting, pad bytes; `wave` module handles all of it |
| Header seekback/refresh | Manual `file.seek(4); file.write(...)` | `wave.writeframes()` | CPython's `_patchheader()` already does exactly this after every write |
| Slot spec parsing | Custom lexer/parser | 4-line `split(',')` + `split('-', 1)` | Slot specs are simple enough that stdlib string ops suffice — no regex library needed |
| Sample interleaving | Buffer + manual interleave loop | `struct.pack('<' + 'h'*N, *samples)` | One pack call produces the correct interleaved PCM bytes |

**Key insight:** The C++ `PCMWaveFileHandler` manually manages RIFF headers and seekback because C++ has no `wave` module. Python's stdlib `wave` module replicates all that logic. Do not port the C++ handler to Python — use `wave.open()`.

## Common Pitfalls

### Pitfall 1: Sample Rate Not Yet Known on First Frame
**What goes wrong:** `wave.open()` requires `setframerate()` before `writeframes()`. But the sample rate is derived from two consecutive frames — on the very first frame, only one timestamp exists.
**Why it happens:** Frame rate must be set before writing begins, but is only computable from the second occurrence of the same slot.
**How to avoid:** Accumulate the first frame in a pending buffer. On the second frame with the same slot, compute the sample rate, call `_open_wav()`, flush the pending frame, then continue normally. Alternatively, require the user to configure the expected sample rate as an HLA setting and use it directly (simpler, avoids timing arithmetic).
**Warning signs:** `wave.Error: # channels not specified` if `setnchannels()` not called before `writeframes()`.

**Recommended approach for this phase:** Use a two-frame delay: buffer the first frame's data, derive sample rate from the second frame, then open the WAV and write both frames. This avoids adding a fourth HLA setting.

### Pitfall 2: 32-bit WAV Samples Are Signed, Not Unsigned
**What goes wrong:** `struct.pack('<I', value)` writes unsigned 32-bit. Standard PCM WAV 32-bit format requires signed. Players like Audacity will interpret the waveform incorrectly.
**Why it happens:** The C++ LLA stores data in a `U64` and the sign adjustment is conditional on the LLA's Signed setting. The `data` field in FrameV2 may arrive as an unsigned large integer.
**How to avoid:** For 32-bit, clamp and sign-convert: if the raw value from `frame.data['data']` exceeds 2^31-1 (0x7FFFFFFF), treat it as a signed negative: `value = value if value < 0x80000000 else value - 0x100000000`. Then pack with `struct.pack('<i', value)`.
**Warning signs:** WAV plays back at wrong amplitude or inverted waveform; Audacity shows clipped or inverted signal.

### Pitfall 3: Frame Accumulator Drift When Slots Are Skipped
**What goes wrong:** If a slot frame is skipped (e.g., due to an error), the accumulator does not fill, and on the next frame_number rollover the previous frame flushes with a missing slot — which is correct behavior (REQ-11 fills with 0), but if frame_number is not tracked, samples drift by one slot position.
**Why it happens:** The C++ handler uses a modular `mSampleIndex` that wraps; Python's dict accumulator is explicit per slot.
**How to avoid:** Always key the accumulator on `slot` (not sequential index), and always read from the accumulator as `self._accum.get(slot, 0)` in `self._slot_list` order. The dict-keyed approach is immune to slot drift.
**Warning signs:** Channel assignment in the WAV output is wrong — channel 0 has channel 1's data, etc.

### Pitfall 4: `wave.open()` Context Manager Closes on `__exit__`
**What goes wrong:** Using `with wave.open(...) as wav:` causes the file to close when the `with` block exits. Since `decode()` is called repeatedly, the `with` block must span across calls — which means the context manager cannot be used in `decode()`.
**Why it happens:** Standard Python context manager semantics.
**How to avoid:** Open `wave.open()` once and store the result as `self._wav`. Call `self._wav.close()` only in a cleanup path. Do NOT use `with wave.open(...) as wav:` syntax for the streaming-write use case.

### Pitfall 5: `GraphTime` Subtraction Precision Loss
**What goes wrong:** `float(frame_b.start_time - frame_a.start_time)` loses precision for very small deltas (< 1ms according to Saleae forum).
**Why it happens:** GraphTimeDelta stores time as two doubles (ms + fractional ms); float conversion truncates.
**How to avoid:** For sample rate derivation, compute from two frames that are far apart (e.g., the 1st and 100th occurrence of the same slot) to get a more stable average: `rate = 100.0 / float(frame_100.start_time - frame_1.start_time)`. Or accept minor imprecision — WAV sample rate is an integer Hz value, so `round()` absorbs small floating-point errors.
**Warning signs:** Derived sample rate is 47999 or 48001 instead of 48000; or NaN if division by zero (frames at same timestamp).

### Pitfall 6: `wave` Module Max File Size
**What goes wrong:** WAV RIFF format has a 4 GiB limit on the data chunk (32-bit chunk size). For very long captures, the WAV data chunk overflows.
**Why it happens:** `wave.Wave_write` uses a 32-bit `_datawritten` counter. On Python 3, integers are unbounded, but the field written to disk is 4 bytes — wraps at 0xFFFFFFFF.
**How to avoid:** This is out of scope for v1.5 (RF64 is explicitly out of scope in REQUIREMENTS.md). Document the 4 GiB limit in REQ-20/21 README. For Phase 9, no special handling needed.
**Warning signs:** Very long captures produce a WAV file that appears corrupt in players after 4 GiB.

## Code Examples

Verified patterns from official sources and CPython source:

### Opening a WAV File for Streaming Write
```python
# Source: https://docs.python.org/3.8/library/wave.html
import wave

def _open_wav(self, sample_rate: int) -> None:
    n_channels = len(self._slot_list)
    sample_width = self._bit_depth // 8   # 2 for 16-bit, 4 for 32-bit
    self._wav = wave.open(self._output_path, 'wb')
    self._wav.setnchannels(n_channels)
    self._wav.setsampwidth(sample_width)
    self._wav.setframerate(sample_rate)
```

### Writing One Complete Interleaved TDM Frame
```python
# Source: https://docs.python.org/3.8/library/struct.html
# Each TDM frame: one sample per channel, interleaved, little-endian signed PCM
import struct

def _write_wav_frame(self) -> None:
    # fmt: '<hhhh' for 4-channel 16-bit, '<ii' for 2-channel 32-bit, etc.
    fmt = '<' + ('h' if self._bit_depth == 16 else 'i') * len(self._slot_list)
    samples = [self._accum.get(slot, 0) for slot in self._slot_list]
    self._wav.writeframes(struct.pack(fmt, *samples))
    # wave.writeframes() calls _patchheader() internally after every write.
    # This satisfies REQ-14 (periodic header refresh) automatically.
    self._frame_count += 1
```

### Sample Rate Derivation from GraphTime
```python
# Source: https://discuss.saleae.com/t/how-to-get-the-duration-of-each-pulse-captured-for-ppm-analyzer/1414
# frame.start_time is a saleae.data.GraphTime object.
# Subtraction yields a GraphTimeDelta; float() converts to seconds.

def _try_derive_sample_rate(self, frame) -> None:
    slot = frame.data['slot']
    if slot not in self._timing_ref:
        self._timing_ref[slot] = frame.start_time
    elif self._sample_rate is None:
        delta_sec = float(frame.start_time - self._timing_ref[slot])
        if delta_sec > 0:
            self._sample_rate = round(1.0 / delta_sec)
```

### Sign Clamping for 32-bit Samples
```python
# For 32-bit PCM WAV: standard format is signed. Clamp if data arrived as unsigned.
def _as_signed(value: int, bit_depth: int) -> int:
    """Convert unsigned-style integer to signed if needed."""
    max_unsigned = (1 << bit_depth) - 1
    half = 1 << (bit_depth - 1)
    value = value & max_unsigned  # mask to bit_depth bits
    if value >= half:
        value -= (1 << bit_depth)
    return value

# Usage:
sample = _as_signed(frame.data['data'], self._bit_depth)
```

### Slot Spec Parser
```python
# Source: standard Python idiom — no library
def parse_slot_spec(spec: str) -> list:
    slots = []
    for token in spec.split(','):
        token = token.strip()
        if not token:
            continue
        if '-' in token:
            a, b = token.split('-', 1)
            slots.extend(range(int(a.strip()), int(b.strip()) + 1))
        else:
            slots.append(int(token))
    if not slots:
        raise ValueError(f"No slots parsed from: {spec!r}")
    return sorted(set(slots))
```

### The Key Insight: `decode()` Skeleton for Phase 9

```python
def decode(self, frame: AnalyzerFrame):
    # Guard: skip non-slot frames (advisory, etc.)
    if frame.type != 'slot':
        return None

    slot = frame.data['slot']
    frame_num = frame.data['frame_number']

    # Guard: skip slots not in our filter set
    if slot not in self._slot_set:
        return None

    # Guard: skip frames flagged as error — write silence instead
    if frame.data.get('short_slot') or frame.data.get('bitclock_error'):
        # Don't read data; accumulator.get(slot, 0) will provide silence
        pass
    else:
        sample = _as_signed(frame.data['data'], self._bit_depth)
        self._accum[slot] = sample

    # Sample rate derivation (uses first two appearances of same slot)
    self._try_derive_sample_rate(frame)

    # Detect TDM frame boundary (frame_number changed = previous frame complete)
    if self._last_frame_num is not None and frame_num != self._last_frame_num:
        if self._sample_rate is not None and self._wav is None:
            self._open_wav(self._sample_rate)
        if self._wav is not None:
            self._write_wav_frame()
        self._accum = {}

    self._last_frame_num = frame_num
    return None
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Manual RIFF header management (C++ style) | `wave.open()` stdlib module | N/A — Python always had `wave` | No header byte math needed in Python |
| External `scipy.io.wavfile` or `soundfile` | `wave` stdlib only | N/A — third-party not available in Logic 2 | Stick to stdlib; no pip in Logic 2 embedded Python |
| Frame-by-frame `writeframes()` per sample | One `writeframes()` per TDM frame (N channels) | Best practice | Reduces seek overhead; more idiomatic |

**Deprecated/outdated:**
- `audioop` module (Python 3.8): deprecated in 3.11, removed in 3.13. Not relevant here since we use `struct.pack` for sample packing.
- `wave.Wave_write.setnframes()`: Setting this manually before writing is outdated practice. `writeframes()` maintains the count automatically.

## Open Questions

1. **Sample rate derivation: stability**
   - What we know: `float(frame_b.start_time - frame_a.start_time)` has precision loss < 1ms per Saleae forum
   - What's unclear: Whether typical TDM at 48 kHz (period ≈ 20.8 µs) will produce enough precision
   - Recommendation: Derive from the first two same-slot frames; use `round()` to snap to integer Hz. If result is wildly off (e.g. < 1000 or > 200000), fall back to 48000 with a status frame warning.

2. **Whether `frame.data['data']` is always signed or always unsigned**
   - What we know: The C++ LLA applies `ConvertToSignedNumber()` only when the LLA setting "Signed" is enabled; otherwise raw `mData1` (unsigned) is stored in `data`. The HLA cannot inspect the LLA's Signed setting.
   - What's unclear: Whether Logic 2 passes Python integers as signed or unsigned for negative C++ S64 values
   - Recommendation: Apply `_as_signed()` clamping unconditionally. If the value is already correctly signed by the LLA, the clamp is a no-op for values in range. This makes the HLA robust regardless of the LLA's Signed setting.

3. **Frame accumulator flush at capture end**
   - What we know: No destructor/finalizer hook exists. The last partial TDM frame (if capture ends mid-frame) will sit in `self._accum` and never be flushed.
   - What's unclear: Whether the last partial frame should be silently dropped or flushed with silence
   - Recommendation: Drop the partial frame. The WAV header is always current due to `writeframes()` calling `_patchheader()` automatically. The file is playable up to the last complete frame.

## Sources

### Primary (HIGH confidence)
- CPython 3.8 `wave.py` source (github.com/python/cpython/blob/3.8/Lib/wave.py) — Wave_write methods: `writeframes()`, `_patchheader()`, `_write_header()`, `close()`
- https://docs.python.org/3.8/library/wave.html — Official API: `setnchannels`, `setsampwidth`, `setframerate`, `writeframes`, behavior of seekable vs unseekable streams
- https://docs.python.org/3.8/library/struct.html — Format codes: `'<h'` (16-bit signed LE), `'<i'` (32-bit signed LE)
- `/src/TdmAnalyzer.cpp` (read directly) — `frame_v2.AddInteger("data", adjusted_value)` confirms data is sign-adjusted when LLA Signed=true; `ConvertToSignedNumber` used
- `/src/TdmAnalyzerResults.cpp` (read directly) — C++ seekback pattern: `updateFileSize()` with `mFile.seekp(RIFF_CKSIZE_POS)` + `mFile.seekp(DATA_CKSIZE_POS)`; Python `wave._patchheader()` is the exact equivalent

### Secondary (MEDIUM confidence)
- https://discuss.saleae.com/t/graphtimedelta-with-high-resolution/2763 — Confirms `float(t - self.first_pulse_time_stamp)` for GraphTimeDelta-to-seconds conversion; warns about precision loss > 1ms
- https://discuss.saleae.com/t/how-to-get-the-duration-of-each-pulse-captured-for-ppm-analyzer/1414 — Confirms `float(frame.end_time - frame.start_time)` pattern; `frame.start_time` is `saleae.data.GraphTime`
- Phase 8 RESEARCH.md (project) — FrameV2 schema confirmed: `slot` (int), `data` (int, sign-adjusted), `frame_number` (int), `short_slot` (bool), etc.

### Tertiary (LOW confidence)
- WebSearch result: "wave.writeframes() patches header automatically" — verified against CPython source (promoted to HIGH)
- WebSearch result: "GraphTime.float() precision loss" — single forum mention, not cross-verified with official API docs

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — `wave` and `struct` are stdlib, verified against Python 3.8 docs and CPython source
- Architecture (accumulator + flush pattern): HIGH — directly mirrors the verified C++ `PCMWaveFileHandler::addSample()` pattern
- `writeframes()` auto-patch behavior: HIGH — verified in CPython 3.8 `wave.py` source
- GraphTime arithmetic: MEDIUM — two consistent Saleae forum posts; not cross-verified with official API docs
- Sample sign handling: MEDIUM — C++ source confirms sign adjustment is conditional; Python behavior for large unsigned ints needs runtime validation
- Pitfalls: MEDIUM — derived from C++ implementation inspection, Python `wave` source, and Saleae forum posts

**Research date:** 2026-03-02
**Valid until:** 2026-09-01 — Python `wave` stdlib is stable; Saleae HLA API unchanged for 3+ years
