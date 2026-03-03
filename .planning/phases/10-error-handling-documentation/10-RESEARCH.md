# Phase 10: Error Handling & Documentation - Research

**Researched:** 2026-03-02
**Domain:** Logic 2 HLA error surface patterns, Python exception hardening, Markdown README docs
**Confidence:** HIGH

---

## Summary

Phase 10 has two independent sub-domains: (1) hardening the HLA error paths so that bad config and LLA error frames never silently crash or corrupt the WAV, and (2) writing the README section that teaches users how to install and configure the HLA.

The HLA's `result_types` dict already declares `'error'` and `'status'` frame types, and `AnalyzerFrame(type, start_time, end_time, data)` is the only constructor available. The challenge for error surface is that `__init__` runs before any frames arrive — if `parse_slot_spec` raises there, Logic 2 shows a generic crash rather than a readable error. The correct pattern is to catch exceptions in `__init__`, store an error message, and then emit an `AnalyzerFrame('error', ...)` on the very first call to `decode()`. This is the standard Saleae HLA error pattern and is used in official Saleae examples.

For the README, the install mechanic is: Extensions panel -> three-dots menu -> "Load Existing Extension..." -> navigate to `hla/` -> select `extension.json`. This is a single-step load; no separate directory preference is needed. The README section should cover: install, settings (slots, output_path, bit_depth), absolute path requirement, and a worked example.

**Primary recommendation:** Wrap `__init__` logic in try/except; emit a deferred `AnalyzerFrame('error', ...)` on first `decode()` call if init failed. Handle LLA error frames (severity field, short_slot, bitclock_error) as silence already done in Phase 9 — verify REQ-18 and REQ-19 are fully covered. Write README `## HLA: TDM WAV Export` section covering REQ-20, REQ-21, REQ-22.

---

## Standard Stack

### Core

| Component | Version | Purpose | Why Standard |
|-----------|---------|---------|--------------|
| `saleae.analyzers.AnalyzerFrame` | Logic 2 HLA API | Emit error/status frames to Logic 2 UI | Only mechanism available; constructor is `AnalyzerFrame(type, start_time, end_time, data)` |
| Python `try/except` | Python 3.8 (stdlib) | Guard `__init__` and `decode()` against crashes | No alternative — Logic 2 has no other error surface mechanism |
| Python `logging` | Python 3.8 (stdlib) | Log warnings for LLA error frames without raising | Standard Python practice; print() also works in Logic 2 but doesn't survive headless runs |

### AnalyzerFrame Constructor (verified from official example)

```python
# Source: https://github.com/saleae/hla-i2c-transactions/blob/master/HighLevelAnalyzer.py
AnalyzerFrame(
    type,         # str  — must be a key in result_types
    start_time,   # GraphTime — from the incoming frame
    end_time,     # GraphTime — from the incoming frame
    data          # dict — arbitrary key/value pairs shown in UI
)
```

`decode()` can return: a single `AnalyzerFrame`, a `list` of `AnalyzerFrame` objects, or `None`.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Deferred error frame | raise in `__init__` | raise causes a generic Logic 2 crash dialog — no user-readable message |
| Deferred error frame | `SetErrorText` | Not available in HLA Python API (C++ SDK only) |
| `logging.warning()` | `print()` | print() works fine in Logic 2 embedded Python; logging is cleaner |

---

## Architecture Patterns

### Pattern 1: Deferred Init Error Surface

**What:** Catch `ValueError` (and `Exception`) in `__init__`. Store error string. Emit `AnalyzerFrame('error', ...)` on the first `decode()` call, then return immediately for all subsequent calls.

**When to use:** Any time `__init__` config validation can fail — e.g., invalid `slots` spec or empty `output_path`.

**Why:** Logic 2 calls `__init__` once at load time before any frames arrive. If `__init__` raises, the HLA becomes unconfigured with no user-visible message. Deferring the error into `decode()` ensures the error appears in the Logic 2 protocol table where the user can see it.

**Example:**
```python
# Source: Saleae HLA pattern (verified from hla-i2c-transactions + official docs)

def __init__(self):
    self._init_error = None
    try:
        self._slots_raw = self.slots
        self._output_path = self.output_path
        self._bit_depth = int(self.bit_depth or '16')
        self._slot_list = parse_slot_spec(self._slots_raw)
        self._slot_set = set(self._slot_list)
        # ... other init ...
    except Exception as e:
        self._init_error = str(e)
        # Initialize safe defaults so decode() can reference attributes
        self._slot_list = []
        self._slot_set = set()
        self._wav = None
        self._sample_rate = None
        self._accum = {}
        self._last_frame_num = None
        self._timing_ref = {}
        self._frame_count = 0

def decode(self, frame: AnalyzerFrame):
    if self._init_error:
        # Emit one error frame and stay silent for all subsequent frames
        # (returning None suppresses further output)
        err = AnalyzerFrame('error', frame.start_time, frame.end_time,
                            {'message': self._init_error})
        self._init_error = None  # clear so subsequent frames return None silently
        return err

    if frame.type != 'slot':
        return None
    # ... normal decode ...
```

**Important:** The `result_types` dict already contains `'error': {'format': 'Error: {{data.message}}'}` — this is already wired up from Phase 8. No changes to `result_types` needed.

### Pattern 2: LLA Error Frame Handling (REQ-18 and REQ-19)

**What:** Phase 9 already guards against `short_slot` and `bitclock_error` by not accumulating those frames (they contribute silence via `_accum.get(slot, 0)`). REQ-19 adds: frames where `frame.data.get('severity') == 'error'` must also not raise.

**Current state (Phase 9):** The existing guard is:
```python
if not (frame.data.get('short_slot') or frame.data.get('bitclock_error')):
    self._accum[slot] = _as_signed(frame.data['data'], self._bit_depth)
```

This already satisfies REQ-18 (silence for short_slot and bitclock_error). REQ-19 requires that severity-error frames don't raise — since `decode()` only accesses `frame.data['slot']` and `frame.data['frame_number']` unconditionally, and those fields are always present in TDM LLA frames (even error ones), REQ-19 may already be satisfied. However, a defensive `frame.data.get('data', 0)` guard on the data field access is prudent for truly malformed frames.

**Recommendation:** Verify REQ-18 and REQ-19 are covered by existing Phase 9 code. If `frame.data['data']` could be missing on an error frame (check LLA schema), add a `.get('data', 0)` guard. Add a comment noting the LLA guarantees these fields exist.

### Pattern 3: Empty output_path Validation

**What:** REQ-16 requires a clear error when `output_path` is empty or unset.

**Current state:** `self._output_path = self.output_path` captures whatever Logic 2 injects. If it is `''` or `None`, `wave.open(self._output_path, 'wb')` will either raise `FileNotFoundError` or open a file named `''` — neither is user-friendly.

**Pattern:** Check `output_path` in `__init__` before storing:
```python
if not self.output_path or not self.output_path.strip():
    raise ValueError("output_path is required — enter an absolute path to the .wav file")
```
This feeds into the deferred error pattern above.

### Anti-Patterns to Avoid

- **Raising in `__init__` without try/except:** Logic 2 shows a generic crash dialog, not the error message.
- **Emitting error frames for every subsequent frame after init failure:** Flood the protocol table. Emit one, then go silent (return None).
- **Accessing `frame.data['data']` without a guard on error frames:** Safe for TDM LLA (schema always includes `data`), but add a comment confirming this assumption.
- **Logging with `logging.basicConfig()` inside the module body:** May interfere with Logic 2's embedded Python logging. Use `logging.getLogger(__name__)` or simply `print()`.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Error display to user | Custom exception hierarchy | `AnalyzerFrame('error', ...)` with `result_types` | Only mechanism Logic 2 exposes to HLA authors |
| Config validation | Complex validator class | Simple `if not x: raise ValueError(...)` in `__init__` | Settings are 3 strings — over-engineering adds no value |
| LLA error filtering | Second pass over frames | Existing `frame.data.get('short_slot')` guard | Already implemented in Phase 9 |

**Key insight:** The Logic 2 HLA API has no equivalent of `SetErrorText`, `sys.exit()`, or any other out-of-band error channel. `AnalyzerFrame('error', ...)` in the protocol table is the only user-visible error surface.

---

## Common Pitfalls

### Pitfall 1: Raising in `__init__` (the silent crash trap)
**What goes wrong:** `parse_slot_spec("")` raises `ValueError`. Logic 2 catches it and marks the HLA as failed with a non-descriptive error. The user sees "Analyzer failed to load" with no useful message.
**Why it happens:** Logic 2 wraps `__init__` but doesn't surface Python exception text in a readable way.
**How to avoid:** Wrap the entire `__init__` body in `try/except Exception as e: self._init_error = str(e)`. Emit the error on first `decode()` call.
**Warning signs:** HLA appears grayed out or "failed" in the analyzer chain immediately after adding it.

### Pitfall 2: Emitting error frames for every frame after failure
**What goes wrong:** `decode()` emits `AnalyzerFrame('error', ...)` for every frame when init failed — floods the protocol table with thousands of identical error rows.
**Why it happens:** Forgetting to clear `self._init_error` after the first emission.
**How to avoid:** Clear `self._init_error = None` after emitting the first error frame. All subsequent calls return `None`.

### Pitfall 3: Missing `severity` field on error frame assumption
**What goes wrong:** Assuming `frame.data['severity']` exists and testing it — but the TDM LLA v1.4 schema uses the boolean flags (`short_slot`, `bitclock_error`, etc.), not a Python `severity` string for filtering.
**Why it happens:** REQUIREMENTS.md REQ-19 mentions "severity = error" but the LLA may use a different field name or type.
**How to avoid:** Check the actual LLA FrameV2 schema. From STATE.md: the ten fields are `slot`, `data`, `frame_number`, `severity`, and 5 boolean error flags. The `severity` field exists — but the existing Phase 9 guard checks the booleans, which is sufficient. REQ-19 is likely already satisfied.

### Pitfall 4: Relative paths in output_path
**What goes wrong:** User enters `output.wav` — `wave.open()` writes relative to Logic 2's CWD (usually the install directory), not the user's home.
**Why it happens:** Python `open()` resolves relative paths against `os.getcwd()`, which in Logic 2 embedded Python is the app install directory.
**How to avoid:** The validation added for REQ-16 can also check `os.path.isabs(path)` and add a clear message. Already noted in KEY DECISIONS in STATE.md.

### Pitfall 5: Assuming `frame.data['data']` exists on all error frames
**What goes wrong:** If the LLA emits an error frame without a `data` field (e.g., a malformed advisory-type frame that somehow passes the `frame.type != 'slot'` guard), `frame.data['data']` raises `KeyError`, crashing `decode()`.
**Why it happens:** Defensive programming gap.
**How to avoid:** Use `frame.data.get('data', 0)` instead of `frame.data['data']` in the accumulation step. Minimal change, big safety improvement.

---

## Code Examples

### Deferred Error Frame Emission

```python
# Source: Saleae HLA pattern — verified against hla-i2c-transactions official example
# https://github.com/saleae/hla-i2c-transactions/blob/master/HighLevelAnalyzer.py

def __init__(self):
    self._init_error = None
    try:
        # All validation here — raises ValueError on bad input
        if not self.output_path or not self.output_path.strip():
            raise ValueError(
                "output_path is required. Enter an absolute path, e.g. /home/user/capture.wav"
            )
        self._slot_list = parse_slot_spec(self.slots)
        # ... rest of init ...
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

def decode(self, frame: AnalyzerFrame):
    if self._init_error is not None:
        err_msg = self._init_error
        self._init_error = None  # clear — emit only once, then silence
        return AnalyzerFrame('error', frame.start_time, frame.end_time,
                             {'message': err_msg})
    # ... normal decode body ...
```

### Graceful LLA Error Frame Handling

```python
# REQ-18: silence for short_slot / bitclock_error (already in Phase 9)
# REQ-19: don't raise on severity-error frames
if not (frame.data.get('short_slot') or frame.data.get('bitclock_error')):
    self._accum[slot] = _as_signed(frame.data.get('data', 0), self._bit_depth)
# If error flags are set, _accum[slot] is not written.
# _write_wav_frame() writes self._accum.get(slot, 0) = 0 (silence) for that slot.
```

### README HLA Section Structure (REQ-20, REQ-21, REQ-22)

```markdown
## HLA: TDM WAV Export

The `hla/` directory contains a Logic 2 High Level Analyzer (HLA) that exports
selected TDM slots to a WAV file in real time during capture.

### Installation

1. In Logic 2, open the **Extensions** panel (right sidebar).
2. Click the **three-dots** menu icon (⋮) at the top of the panel.
3. Select **"Load Existing Extension..."**.
4. Navigate to the `hla/` folder in this repository.
5. Select `extension.json` and click **Open**.
6. The extension appears as **"TDM WAV Export"** in the Extensions panel.

To use the HLA, add it to your analyzer chain after the **TdmAnalyzer** LLA.

### Settings

| Setting | Description | Example |
|---------|-------------|---------|
| **Slots** | Comma-separated slot indices or hyphenated ranges to export as WAV channels | `0,2` or `0-3` or `1,3-5,7` |
| **Output Path** | **Absolute** path to the output `.wav` file | `/home/user/captures/output.wav` |
| **Bit Depth** | Sample bit depth: `16` (default) or `32` | `16` |

### Absolute Paths Required

The `output_path` setting must be an absolute path. Relative paths resolve
against the Logic 2 application directory, not your working directory.

- **Linux/macOS:** `/home/user/captures/output.wav`
- **Windows:** `C:\Users\user\captures\output.wav`

### How It Works

- The HLA reads frames from the upstream TdmAnalyzer LLA.
- Only the slots listed in **Slots** are written — others are discarded.
- Channels appear in the WAV in the order slots were specified (not sorted).
- The WAV header is updated after every frame, so partial captures are playable.
- Error frames from the LLA (short slot, bitclock error) produce silence in the WAV rather than crashing.
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|-----------------|--------------|--------|
| Raise in HLA `__init__` on bad config | Catch, store, emit error frame on first decode | Always the right pattern — official Saleae examples use error frames | User sees readable error in protocol table |
| `SetErrorText` (C++ SDK only) | `AnalyzerFrame('error', ...)` in Python HLA | N/A — Python HLA API never had SetErrorText | No alternative; error frames are the only surface |

**Deprecated/outdated:**
- Using `print()` to stderr as the only error mechanism: Output goes to Logic 2's console log, not visible to most users. Always pair with an error frame.

---

## Phase Requirements Coverage

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| REQ-16 | If `output_path` is empty or unset, HLA surfaces a clear error via its frame output | Empty/None check in `__init__` + deferred `AnalyzerFrame('error', ...)` on first `decode()` call |
| REQ-17 | If `slots` is invalid (unparseable), HLA surfaces a clear error | `parse_slot_spec` raises `ValueError` — caught in `__init__` try/except, surfaced via deferred error frame |
| REQ-18 | Error frames from LLA (short slot, extra slot, bitclock error) are written as silence | Phase 9 boolean guard `not (short_slot or bitclock_error)` already covers this; add `.get('data', 0)` guard |
| REQ-19 | HLA gracefully handles frames with severity = error — logs them, does not raise | Phase 9 guard likely covers this; verify `severity` field type; add defensive `.get()` on `data` field |
| REQ-20 | README section explains how to install the HLA in Logic 2 | Extensions panel -> three-dots -> "Load Existing Extension..." -> select extension.json (verified from Saleae docs) |
| REQ-21 | README section explains the settings fields with examples | Table of slots / output_path / bit_depth with examples |
| REQ-22 | README notes the requirement for absolute paths in `output_path` | Explicit "Absolute Paths Required" sub-section |
</phase_requirements>

---

## Open Questions

1. **Does the LLA always emit `frame.data['data']` on error frames?**
   - What we know: The ten-field FrameV2 schema in STATE.md includes `data` as one of the ten fields. Phase 9 plan says all fields are emitted unconditionally.
   - What's unclear: Whether `data` is literally 0 or could be absent on a `short_slot` frame.
   - Recommendation: Use `frame.data.get('data', 0)` defensively regardless. Zero cost, significant safety gain.

2. **Does Logic 2 show `AnalyzerFrame('error', ...)` output when the HLA is in a chain but emits no annotation frames for normal operation (decode() returns None)?**
   - What we know: Official Saleae examples use error frames freely alongside None returns.
   - What's unclear: Whether Logic 2 requires at least one non-None frame to display the error frame.
   - Recommendation: Implement as designed — the error frame on first decode() is almost certain to display. If not, the fallback is to keep returning the error frame (not clear `_init_error`).

3. **Should the README HLA section go before or after the existing LLA export docs?**
   - What we know: Current README has a "Exporting data as a wave file" section covering the C++ LLA's WAV export. The HLA is a separate tool serving a different use case.
   - Recommendation: Add the HLA section as its own top-level `##` heading after the existing "Exporting data" section. Distinguish it clearly as "Python HLA (separate from the LLA export)".

---

## Sources

### Primary (HIGH confidence)
- https://github.com/saleae/hla-i2c-transactions/blob/master/HighLevelAnalyzer.py — Official Saleae HLA example: `AnalyzerFrame` constructor pattern, `result_types` with `'error'` type, deferred error frame pattern
- Saleae support docs (search results) — "Load Existing Extension..." install mechanic confirmed; "three-dots menu -> select extension.json" pattern
- `/home/chris/gitrepos/saleae_tdm_analyer/hla/TdmWavExport.py` — Phase 9 implementation: existing error guards for `short_slot`, `bitclock_error`
- `/home/chris/gitrepos/saleae_tdm_analyer/.planning/REQUIREMENTS.md` — REQ-16 through REQ-22 verbatim
- `/home/chris/gitrepos/saleae_tdm_analyer/.planning/STATE.md` — Key decisions: absolute paths required, ten-field FrameV2 schema

### Secondary (MEDIUM confidence)
- WebFetch of HLA Extensions support page — confirmed `AnalyzerFrame(type, start_time, end_time, data)` constructor, `decode()` return options (single frame, list, or None)
- WebSearch on Logic 2 extension installation — "Load Existing Extension..." confirmed from Saleae docs search result snippets

### Tertiary (LOW confidence)
- None identified — all critical claims have PRIMARY source backing.

---

## Metadata

**Confidence breakdown:**
- Error handling patterns: HIGH — verified from official Saleae HLA example (hla-i2c-transactions) and HLA API docs
- LLA error frame coverage: HIGH — Phase 9 implementation is readable and guards are present; `.get()` improvement is defensive
- HLA install steps: MEDIUM — confirmed from search result snippets of official docs; exact UI wording may vary by Logic 2 version
- README content: HIGH — requirements are clear, content is deterministic given the project state

**Research date:** 2026-03-02
**Valid until:** 2026-09-02 (Saleae HLA API is stable; install UI may change in new Logic 2 releases)
