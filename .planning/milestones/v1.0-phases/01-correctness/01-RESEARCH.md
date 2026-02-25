# Phase 1: Correctness - Research

**Researched:** 2026-02-24
**Domain:** C++ Saleae Logic 2 Protocol Analyzer Plugin — surgical bug fixes in existing source files
**Confidence:** HIGH

---

## Summary

Phase 1 addresses four concrete, well-localized correctness defects in the existing TDM analyzer codebase. All defects have been identified at specific file and line locations through direct source inspection. No new libraries, patterns, or architectural changes are required — every fix is a targeted edit within existing functions, ranging from a one-line substitution to a handful of lines of new code.

The four bugs are: (1) `sprintf` into a fixed `char[80]` buffer with a suppressed compiler warning, (2) the export file type UI control initialized with the wrong variable in the settings constructor, (3) WAV channel alignment drift when a `SHORT_SLOT` error frame skips an `addSample()` call, and (4) `GenerateFrameTabularText()` already has `ClearTabularText()` — this requires verification rather than a new fix. Each fix has a clear correct implementation documented in the prior pitfalls research.

The most important planning insight is that the fixes are mechanically independent: CORR-01 (sprintf), CORR-02 (wrong variable), CORR-03 (WAV alignment), and CORR-04 (ClearTabularText audit) touch different functions in different files and can be planned as separate tasks in any order. There are no dependencies between them within Phase 1.

**Primary recommendation:** Fix all four in dedicated commits — one commit per bug — so each change is reviewable in isolation and the git history tells a clear story.

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| CORR-01 | Fix sprintf buffer overflow risk — replace `sprintf` with `snprintf` at all three call sites; remove `#pragma warning(disable: 4996)` suppressions | Buffer analysis confirmed: 80-byte buffer holds all four simultaneous errors at current string lengths but has no margin. Three distinct call sites identified. Pragma locations confirmed. |
| CORR-02 | Fix settings constructor using wrong variable (`mEnableAdvancedAnalysis` instead of `mExportFileType`) | Bug confirmed at `TdmAnalyzerSettings.cpp` line 136. `UpdateInterfacesFromSettings()` at line 191 uses correct variable. One-line fix. |
| CORR-03 | Fix WAV channel alignment drift after SHORT_SLOT frames — zero-fill missing slot to preserve channel mapping | Root cause confirmed: `GenerateWAV()` skips `addSample()` for SHORT_SLOT frames (lines 180-189), causing `mSampleIndex` to drift for all subsequent frames. Fix: call `addSample(0)` as placeholder. |
| CORR-04 | Audit `GenerateFrameTabularText()` for `ClearTabularText()` compliance — ensure it is called before any `AddTabularText()` | Source verified: `ClearTabularText()` IS present at line 471, before `GetFrame()` and all `sprintf`/`AddTabularText()` calls. CORR-04 requires confirming this, not adding it. |
</phase_requirements>

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Saleae AnalyzerSDK | HEAD @ `114a3b8` | All SDK APIs used in fixes | Only SDK supported; already in use |
| C++11 | — | Language standard | SDK mandates C++11; ABI constraint |
| CMake 3.13+ | — | Build system | Already in use; FetchContent pattern |

### Supporting

No new libraries are added in Phase 1. All fixes use only the C standard library (`<cstring>`, `<stdio.h>`) and C++ standard library facilities already included in the affected files.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `snprintf` (keep fixed char arrays) | `std::string` concatenation | `std::string` is cleaner but requires more refactoring; `snprintf` is a minimal, surgical fix that changes the least code and is easiest to review |
| Direct `addSample(0)` call for SHORT_SLOT | Track slot index separately | Direct call is simpler; slot index tracking would require more state changes in `PCMWaveFileHandler` |

**Installation:** No new dependencies. No install step needed.

---

## Architecture Patterns

### Relevant Project Structure

```
src/
├── TdmAnalyzer.cpp           # CORR-01: sprintf at lines 311-329
├── TdmAnalyzerSettings.cpp   # CORR-01: #pragma at line 8; CORR-02: bug at line 136
├── TdmAnalyzerResults.cpp    # CORR-01: #pragma at line 11, sprintf at lines 51-71, 490-510
│                             # CORR-03: GenerateWAV() at lines 176-193
│                             # CORR-04: GenerateFrameTabularText() at lines 466-514
└── TdmAnalyzerResults.h      # (read-only for Phase 1; WAV structs not in scope)
```

### Pattern 1: snprintf with Running Offset

**What:** Replace chained `sprintf(buf + strlen(buf), ...)` with `snprintf` using an explicit tracked offset variable. Each error string appended must pass the remaining capacity.

**When to use:** Wherever the existing chained sprintf pattern appears (three locations in Phase 1).

**Example (from PITFALLS.md):**
```cpp
// BEFORE (unsafe — three locations in codebase):
char error_str[80] = "";
if(frame.mFlags & SHORT_SLOT)
    sprintf(error_str, "E: Short Slot ");
if(frame.mFlags & MISSED_DATA)
    sprintf(error_str + strlen(error_str), "E: Data Error ");
if(frame.mFlags & MISSED_FRAME_SYNC)
    sprintf(error_str + strlen(error_str), "E: Frame Sync Missed ");
if(frame.mFlags & BITCLOCK_ERROR)
    sprintf(error_str + strlen(error_str), "E: Bitclock Error ");

// AFTER (safe — snprintf with remaining capacity):
char error_str[80] = "";
size_t used = 0;
if(frame.mFlags & SHORT_SLOT) {
    used += snprintf(error_str + used, sizeof(error_str) - used, "E: Short Slot ");
}
if(frame.mFlags & MISSED_DATA) {
    used += snprintf(error_str + used, sizeof(error_str) - used, "E: Data Error ");
}
if(frame.mFlags & MISSED_FRAME_SYNC) {
    used += snprintf(error_str + used, sizeof(error_str) - used, "E: Frame Sync Missed ");
}
if(frame.mFlags & BITCLOCK_ERROR) {
    used += snprintf(error_str + used, sizeof(error_str) - used, "E: Bitclock Error ");
}
```

**Note:** `snprintf` returns the number of characters written (not counting the null terminator), even if truncated. Accumulating the return value into `used` is safe — if `used >= sizeof(error_str)`, subsequent calls write nothing and are no-ops.

**The same pattern also applies to `warning_str[32]`** (the `UNEXPECTED_BITS` / Extra Slot string). That buffer currently only appends one string, so it cannot overflow, but it should be converted to `snprintf` for consistency when removing the `#pragma warning` suppression.

### Pattern 2: Constructor Interface Initialization Consistency

**What:** Every `mXxxInterface->SetNumber(mXxx)` call in the constructor must use the same member variable as the corresponding `mXxxInterface->SetNumber(mXxx)` call in `UpdateInterfacesFromSettings()`.

**When to use:** As a verification step for CORR-02 and as a check-pattern when reviewing any future settings interface addition.

**Example — the bug and its fix:**
```cpp
// TdmAnalyzerSettings.cpp constructor — BEFORE (line 136):
mExportFileTypeInterface->SetNumber( mEnableAdvancedAnalysis );  // BUG: wrong variable

// TdmAnalyzerSettings.cpp UpdateInterfacesFromSettings() — line 191 (already correct):
mExportFileTypeInterface->SetNumber( mExportFileType );          // CORRECT

// Fix: change constructor line 136 to:
mExportFileTypeInterface->SetNumber( mExportFileType );
```

### Pattern 3: WAV Channel Alignment Preservation

**What:** When iterating frames in `GenerateWAV()`, SHORT_SLOT frames must still contribute a sample (zero) to keep `PCMWaveFileHandler::mSampleIndex` synchronized with the expected channel position.

**When to use:** In the `GenerateWAV()` frame iteration loop, wherever `addSample()` is conditionally skipped.

**Example — the fix:**
```cpp
// BEFORE (TdmAnalyzerResults.cpp lines 176-189 — channel drift occurs):
for( U64 i = 0; i < num_frames; i++ )
{
    Frame frame = GetFrame( i );
    if(frame.mType < num_slots_per_frame)
    {
        wave_file_handler.addSample( frame.mData1 );
        // ... cancel check ...
    }
}

// AFTER (zero-fill SHORT_SLOT frames to preserve channel alignment):
for( U64 i = 0; i < num_frames; i++ )
{
    Frame frame = GetFrame( i );
    if(frame.mType < num_slots_per_frame)
    {
        // For SHORT_SLOT frames, write silence (0) to preserve channel alignment.
        // Without this, mSampleIndex drifts and subsequent channels are mapped
        // to the wrong WAV channel for all remaining frames.
        U64 sample_value = (frame.mFlags & SHORT_SLOT) ? 0 : frame.mData1;
        wave_file_handler.addSample( sample_value );
        // ... cancel check ...
    }
}
```

### Pattern 4: ClearTabularText Compliance Verification

**What:** `GenerateFrameTabularText()` must call `ClearTabularText()` as its first statement. SDK 1.1.32+ crashes Logic 2 if omitted.

**Current state (VERIFIED):** `TdmAnalyzerResults.cpp` line 471 already has `ClearTabularText()` as the first executable statement in `GenerateFrameTabularText()`. CORR-04 is a verification task, not a code-change task.

```cpp
// TdmAnalyzerResults.cpp line 466 (already correct):
void TdmAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
    char error_str[ 80 ] = "";
    char warning_str[ 32 ] = "";

    ClearTabularText();   // line 471 — PRESENT AND CORRECT

    Frame frame = GetFrame( frame_index );
    // ...
}
```

**CORR-04 action:** Confirm this in the source, add a code comment noting the SDK requirement and why it must remain first, and close the requirement.

### Anti-Patterns to Avoid

- **Do NOT remove the `char error_str[80]` buffer and replace with `std::string`** — while `std::string` is cleaner, it is a larger refactor that changes more lines and is harder to review. The `snprintf` fix is minimal and reviewable. Phase 1 is surgical fixes only.
- **Do NOT remove `#pragma warning(disable: 4996)` before fixing all `sprintf` calls** — removing the pragma first would cause build failures on MSVC before the fixes are in place. Fix all `sprintf` → `snprintf` first, then remove the pragma.
- **Do NOT change `GenerateWAV()` logic beyond the single zero-fill addition** — the wider WAV export refactor (header seek frequency, 4GB limit) is Phase 3 work.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Safe string formatting | Custom length-tracking wrapper | `snprintf` directly | `snprintf` is C standard, understood by every reviewer, zero overhead |
| Channel alignment tracking | New state variable in PCMWaveFileHandler | Inline conditional in GenerateWAV() | The fix is one line; a new state variable would require header changes and constructor changes |

**Key insight:** Phase 1 is entirely about removing unsafe patterns and correcting one wrong variable reference. No new abstraction is warranted.

---

## Common Pitfalls

### Pitfall 1: Removing Pragma Before Fixing All sprintf Sites

**What goes wrong:** If `#pragma warning(disable: 4996)` is removed from `TdmAnalyzerResults.cpp` and `TdmAnalyzerSettings.cpp` before all `sprintf` calls in those files are converted to `snprintf`, the MSVC build breaks on Windows CI.

**Why it happens:** The fix sequence matters — pragma removal is the last step, after all call sites are converted.

**How to avoid:** Fix all three `sprintf` call sites (TdmAnalyzer.cpp lines 311-329, TdmAnalyzerResults.cpp lines 51-71, TdmAnalyzerResults.cpp lines 490-510), then remove both pragmas.

**Warning signs:** Build failure on Windows CI with C4996 errors after pragma removal.

### Pitfall 2: Missing warning_str in snprintf Conversion

**What goes wrong:** The `warning_str[32]` buffer also uses `sprintf` (for the `UNEXPECTED_BITS` / Extra Slot case). Converting only `error_str` but not `warning_str` leaves a residual `sprintf` use, meaning the pragma cannot be fully removed.

**Why it happens:** The `warning_str` buffer only ever receives one string (no chaining), so it won't overflow — but the pragma removal requires ALL `sprintf` calls to be gone.

**How to avoid:** Convert both `error_str` and `warning_str` sprintf calls to snprintf in all three locations.

### Pitfall 3: CORR-04 Treated as a Code Change (It Isn't)

**What goes wrong:** Assuming `ClearTabularText()` is missing and writing code to add it, creating a diff that duplicates an already-present call.

**Why it happens:** The requirement description says "audit and fix" — but the source shows the call is already there at line 471.

**How to avoid:** Read the source before writing any code. The CORR-04 task is: read lines 466-514, confirm `ClearTabularText()` is first, add explanatory comment, close the requirement.

### Pitfall 4: Wrong Condition for SHORT_SLOT Zero-Fill

**What goes wrong:** Zero-filling when `frame.mType >= num_slots_per_frame` instead of when `frame.mFlags & SHORT_SLOT`. The existing outer `if` already excludes extra-slot frames; the zero-fill applies to frames within the expected slot range that have the SHORT_SLOT flag.

**Why it happens:** Confusing the extra-slot condition (already handled by the `frame.mType < num_slots_per_frame` guard) with the short-slot condition (a frame that started but ended before all bits were received).

**How to avoid:** The condition is `(frame.mFlags & SHORT_SLOT)` inside the existing `if(frame.mType < num_slots_per_frame)` block. Zero-fill only those frames, not extra-slot frames (which are already skipped).

---

## Code Examples

### CORR-01: All Three sprintf Locations

**Location 1 — TdmAnalyzer.cpp lines 309-329 (FrameV2 error string):**
```cpp
// The error_str used for frame_v2.AddString("errors", error_str)
// Pattern: same error flags, same buffer, same chained sprintf
// Fix: apply snprintf pattern shown in Architecture Patterns above
```

**Location 2 — TdmAnalyzerResults.cpp lines 51-71 (GenerateBubbleText):**
```cpp
// error_str[80] and warning_str[32] used in GenerateBubbleText()
// Also: channel_num_str[128] uses sprintf but has fixed-width content (safe, but
//       consider snprintf for consistency once pragma is removed)
// Primary fix target: the error_str chained sprintf lines 53-66
```

**Location 3 — TdmAnalyzerResults.cpp lines 490-510 (GenerateFrameTabularText):**
```cpp
// error_str[80] and warning_str[32] used in GenerateFrameTabularText()
// Same pattern as Location 2; same fix applies
```

**Pragma removal — two files:**
```cpp
// Remove from TdmAnalyzerResults.cpp line 11:
// #pragma warning( disable : 4996 )

// Remove from TdmAnalyzerSettings.cpp line 8:
// #pragma warning( disable : 4996 )
```

**Note:** `TdmAnalyzerSettings.cpp` has the pragma at line 8 but its own `sprintf` calls (if any exist in that file beyond the constructor area) should also be checked. Source inspection shows the primary usage is in `TdmAnalyzer.cpp` and `TdmAnalyzerResults.cpp`.

### CORR-02: Constructor Fix

**File:** `src/TdmAnalyzerSettings.cpp` line 136

```cpp
// BEFORE:
mExportFileTypeInterface->SetNumber( mEnableAdvancedAnalysis );

// AFTER:
mExportFileTypeInterface->SetNumber( mExportFileType );
```

**Verification step:** Compare with `UpdateInterfacesFromSettings()` line 191:
```cpp
mExportFileTypeInterface->SetNumber( mExportFileType );  // already correct — must match after fix
```

### CORR-03: WAV Zero-Fill

**File:** `src/TdmAnalyzerResults.cpp` lines 176-193

```cpp
// BEFORE:
for( U64 i = 0; i < num_frames; i++ )
{
    Frame frame = GetFrame( i );
    if(frame.mType < num_slots_per_frame)
    {
        wave_file_handler.addSample( frame.mData1 );
        if( UpdateExportProgressAndCheckForCancel( i, num_frames ) == true )
        {
            wave_file_handler.close();
            return;
        }
    }
}

// AFTER:
for( U64 i = 0; i < num_frames; i++ )
{
    Frame frame = GetFrame( i );
    if(frame.mType < num_slots_per_frame)
    {
        // SHORT_SLOT frames contribute silence (0) to preserve WAV channel alignment.
        // Skipping addSample() entirely would cause all subsequent channels to drift
        // by one slot position in the output file.
        U64 sample_value = (frame.mFlags & SHORT_SLOT) ? 0 : frame.mData1;
        wave_file_handler.addSample( sample_value );
        if( UpdateExportProgressAndCheckForCancel( i, num_frames ) == true )
        {
            wave_file_handler.close();
            return;
        }
    }
}
```

### CORR-04: Confirming ClearTabularText Compliance

**File:** `src/TdmAnalyzerResults.cpp` lines 466-471

```cpp
// Already correct — ClearTabularText() is the first executable statement:
void TdmAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
    char error_str[ 80 ] = "";
    char warning_str[ 32 ] = "";

    ClearTabularText();  // Required by SDK >= 1.1.32; must remain first executable call.
```

**Action:** Add explanatory comment (shown above) and close CORR-04. No code logic changes required.

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `sprintf` with warning suppression pragma | `snprintf` with explicit capacity | C99 / MSVC 2005+ | Prevents silent buffer overflow; allows pragma removal |
| `ClearResultStrings()` + `AddResultString()` for tabular display | `ClearTabularText()` + `AddTabularText()` | SDK 1.1.32 | Old API deprecated; omitting `ClearTabularText()` crashes Logic 2 |

**Deprecated/outdated:**
- `#pragma warning(disable: 4996)`: Appropriate as a workaround when `sprintf` must be used; unnecessary and masking after conversion to `snprintf`.
- `sprintf(buf + strlen(buf), ...)` chaining: Replaced by `snprintf` with tracked offset.

---

## Open Questions

1. **Does `TdmAnalyzerSettings.cpp` have any `sprintf` calls beyond the pragma?**
   - What we know: The pragma is present at line 8 of `TdmAnalyzerSettings.cpp`. The primary identified `sprintf` sites are in `TdmAnalyzer.cpp` and `TdmAnalyzerResults.cpp`.
   - What's unclear: Whether there are additional `sprintf` calls in `TdmAnalyzerSettings.cpp` that also need conversion before the pragma can be removed.
   - Recommendation: During implementation, `grep` the full file for `sprintf` before removing the pragma. If none are found, the pragma removal from `TdmAnalyzerSettings.cpp` is safe independently of the other file.

2. **8-bit WAV encoding correctness (from SUMMARY.md gap)**
   - What we know: 8-bit PCM WAV must use unsigned offset binary, not signed two's complement. The `PCMWaveFileHandler` handles 8-bit via `mBytesPerChannel = 1`.
   - What's unclear: Whether the current `addSample()` implementation correctly converts signed data to unsigned offset binary for 8-bit depth.
   - Recommendation: Flag for inspection during CORR-03 implementation. If incorrect, document as a separate bug for Phase 3 (it is a data correctness issue but affects only 8-bit depth, which is uncommon for TDM audio). Do not block Phase 1 completion on this.

---

## Sources

### Primary (HIGH confidence)

- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerResults.cpp` — all sprintf locations, GenerateWAV() loop, GenerateFrameTabularText() ClearTabularText() call
- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerSettings.cpp` — constructor bug at line 136, UpdateInterfacesFromSettings() at line 191, pragma at line 8
- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzer.cpp` — sprintf in AnalyzeTdmSlot at lines 311-329
- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerResults.h` — WavePCMHeader struct, PCMWaveFileHandler class
- `.planning/research/PITFALLS.md` — Pitfalls 1, 2, 6 with exact line numbers and fix implementations (HIGH confidence, derived from direct source inspection)
- `.planning/research/ARCHITECTURE.md` — Pattern 3 (ClearTabularText requirement), anti-patterns (HIGH confidence)
- `.planning/codebase/CONCERNS.md` — Known bugs table cross-referencing all four CORR requirements

### Secondary (MEDIUM confidence)

- `.planning/research/SUMMARY.md` — Phase 1 key actions, confidence assessment (MEDIUM: summarizes PRIMARY sources)

---

## Metadata

**Confidence breakdown:**
- Bug locations and fix implementations: HIGH — verified in actual source files at specific line numbers
- snprintf pattern: HIGH — C standard function, behavior fully documented
- CORR-04 status (already compliant): HIGH — `ClearTabularText()` confirmed at line 471 by direct read
- CORR-02 fix: HIGH — single-line, variable substitution, correct variable confirmed at line 191
- CORR-03 fix: HIGH — root cause understood, fix implementation straightforward

**Research date:** 2026-02-24
**Valid until:** Indefinite — findings are grounded in static source code, not external APIs or ecosystem state
