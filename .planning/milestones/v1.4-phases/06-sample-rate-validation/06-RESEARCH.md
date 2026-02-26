# Phase 6: Sample Rate Validation - Research

**Researched:** 2026-02-25
**Domain:** Saleae Logic 2 C++ Protocol Analyzer Plugin — settings validation and WorkerThread advisory annotation
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **Warning annotation format:** Distinct FrameV2 type (e.g., "advisory") separate from the "slot" frame type — not the nine-field slot schema. Severity field: `warning`. Show the math in the message: actual capture rate, recommended minimum (4x bit clock), and computed bit clock rate. Include a brief explanation of why 4x matters (edge detection reliability). Emitted before the first decoded slot — row 0 in the data table.

- **Error message wording:** Show the math with breakdown: computed bit clock, the parameters that produce it, and the 500 MHz ceiling. Include specific suggestions: "Reduce frame rate, slots per frame, or bits per slot." Hard block is based on raw bit clock rate (frame_rate x slots x bits_per_slot), not the 4x oversampled rate.

- **Threshold behavior:**
  - Soft warning: fires when `sample_rate < 4 * bit_clock` — exactly 4x = no warning (strict `<`)
  - Hard block: fires when `bit_clock > 500 MHz` — exactly 500 MHz is allowed (strict `>`)
  - Named constants: `kMinOversampleRatio = 4` and `kMaxBitClockHz = 500'000'000` (or similar)
  - U64 arithmetic for bit clock calculation to prevent overflow with large parameter combinations
  - Auto-scale units in messages based on magnitude (Hz, kHz, MHz)

- **Settings validation additions:**
  - Zero-parameter guard: reject frame_rate = 0, slots_per_frame = 0, or bits_per_slot = 0 in SetSettingsFromInterfaces
  - Zero-parameter checks go before existing validations (fail on most basic error first)
  - 500 MHz bit clock check goes after parameter extraction and existing validations

- **Warning persistence:**
  - Advisory frame appears as data table row only — no waveform markers
  - `low_sample_rate` boolean added to every slot frame (true when capture rate < 4x bit clock)
  - When `low_sample_rate` is true and no decode errors are present, slot severity is elevated to "warning" (not "ok")

### Claude's Discretion
- Exact advisory frame field names beyond severity (message text field, etc.)
- Formatting helper for auto-scaled unit display
- Whether the advisory type string is "advisory", "warning", or something else
- Sample span for the advisory frame (what sample range it covers)

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| SRAT-01 | Add non-blocking sample rate warning annotation on first analysis frame when capture sample rate is below 4× bit clock rate — uses FrameV2 or tabular text, not SetErrorText | WorkerThread() init (after GetSampleRate() call) → emit advisory FrameV2 before first slot; add low_sample_rate boolean to every slot frame; elevate severity when low_sample_rate true and no errors |
| SRAT-02 | Add hard rejection in SetSettingsFromInterfaces for physically impossible TDM parameter combinations requiring bit clock rate >500 MHz — uses SetErrorText to block analysis | After parameter extraction in SetSettingsFromInterfaces(): U64 bit clock = frame_rate × slots × bits; if > 500,000,000 then SetErrorText + return false; zero-parameter guards go first |
</phase_requirements>

---

## Summary

Phase 6 adds two guardrails to the TDM analyzer. The first (SRAT-02) is a hard block in `SetSettingsFromInterfaces()` that rejects physically impossible configurations at settings-entry time. The second (SRAT-01) is a non-blocking advisory annotation emitted in `WorkerThread()` when the actual Logic 2 capture rate is below 4× the bit clock frequency.

The architecture research from the v1.4 milestone already documented the key constraint: `GetSampleRate()` is not available in `SetSettingsFromInterfaces()` — it is only callable from the `WorkerThread()` context. This means the two guardrails necessarily live in different functions. SRAT-02 (hard block) computes the raw bit clock from settings parameters and compares to 500 MHz — no runtime sample rate needed. SRAT-01 (soft advisory) compares `GetSampleRate()` against `4 * bit_clock` at `WorkerThread()` start.

The advisory FrameV2 frame is a new, distinct frame type (different from "slot") emitted as row 0 before any decoded slot data. It carries a message string showing the math. In addition, every slot frame gains a new `low_sample_rate` boolean field, and slot severity is elevated to "warning" when `low_sample_rate` is true and no decode errors are present.

**Primary recommendation:** Two separate code changes in two files: `TdmAnalyzerSettings.cpp` (SRAT-02, hard block) and `TdmAnalyzer.cpp` (SRAT-01, advisory + per-slot boolean). No new files needed.

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Saleae AnalyzerSDK | master @ `114a3b8` | `FrameV2`, `SetErrorText`, `GetSampleRate`, `AddFrameV2` | Only SDK available for Logic 2 plugins |
| C++ stdlib | C++11 | `snprintf` for message formatting, `constexpr` for named constants | No external dependencies allowed |

### Supporting

No new libraries required. All necessary SDK APIs already in use by the existing codebase.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `snprintf` char buffer for message | `std::string` + `std::to_string` | `snprintf` matches existing code style; zero heap allocation; simpler |
| `AddString("message", ...)` for advisory text | Multiple integer fields for rate/threshold | Single message string with human-readable math is simpler for Logic 2 data table display |
| FrameV2 advisory type string "advisory" | "warning", "info", "sample_rate_warning" | Claude's discretion — any non-"slot" type works; "advisory" is clear and distinct |

---

## Architecture Patterns

### Recommended Project Structure

No new files. All changes go into existing source files:

```
src/
├── TdmAnalyzerSettings.cpp    # SRAT-02: zero-param guard + 500 MHz bit clock hard block
├── TdmAnalyzer.cpp            # SRAT-01: advisory FrameV2 in WorkerThread() + low_sample_rate boolean + severity elevation in AnalyzeTdmSlot()
├── TdmAnalyzerSettings.h      # Named constants: kMaxBitClockHz, kMinOversampleRatio
└── TdmAnalyzer.h              # No change needed (mSampleRate already stored as U64 member)
```

### Pattern 1: Hard Block in SetSettingsFromInterfaces()

**What:** Compute bit clock from settings parameters using U64 arithmetic. Compare to named constant. Call `SetErrorText()` and return false if exceeded.

**When to use:** For configurations that are physically unrealizable — the user cannot proceed because no Logic 2 hardware could ever capture this signal.

**Example:**

```cpp
// Source: ARCHITECTURE.md Feature 4 + CONTEXT.md decisions

// Zero-parameter guard (goes first, before parameter extraction)
// NOTE: mTdmFrameRateInterface->GetInteger() can return 0 if user clears the field
// Add these checks AFTER extracting the values but BEFORE existing bit-depth validation

if( mTdmFrameRate == 0 )
{
    SetErrorText( "Frame rate must be greater than zero" );
    return false;
}
if( mSlotsPerFrame == 0 )
{
    SetErrorText( "Slots per frame must be greater than zero" );
    return false;
}
if( mBitsPerSlot == 0 )
{
    SetErrorText( "Bits per slot must be greater than zero" );
    return false;
}

// 500 MHz bit clock hard block (goes after existing validations)
constexpr U64 kMaxBitClockHz = 500000000ULL;  // 500 MHz — Logic 2 Pro maximum
U64 bit_clock_hz = U64( mTdmFrameRate ) * U64( mSlotsPerFrame ) * U64( mBitsPerSlot );
if( bit_clock_hz > kMaxBitClockHz )
{
    char msg[ 256 ];
    // Auto-scale bit_clock_hz for display (show the math)
    snprintf( msg, sizeof( msg ),
        "TDM configuration requires %" PRIu64 " MHz bit clock "
        "(%" PRIu32 " Hz x %u slots x %u bits), exceeding maximum 500 MHz. "
        "Reduce frame rate, slots per frame, or bits per slot.",
        bit_clock_hz / 1000000ULL,
        mTdmFrameRate, mSlotsPerFrame, mBitsPerSlot );
    SetErrorText( msg );
    return false;
}
```

**Note:** `PRIu64` from `<inttypes.h>` or use a cast + `%llu` — check what existing code uses. The existing codebase uses `U64` and `snprintf` with `%d` for `U32`. For the message, casting to `unsigned long long` with `%llu` avoids the `<inttypes.h>` include.

### Pattern 2: Advisory FrameV2 in WorkerThread() Init

**What:** After `mSampleRate = GetSampleRate()`, compute bit clock, compare to `4 * bit_clock`. If sample rate is below threshold, emit an advisory FrameV2 before the main analysis loop begins.

**When to use:** For marginal configurations where analysis can still run but results may be unreliable. Emitted exactly once, as row 0 in the data table.

**Example:**

```cpp
// Source: CONTEXT.md decisions + ARCHITECTURE.md Feature 4

void TdmAnalyzer::WorkerThread()
{
    // ... existing setup (arrow marker, channel data pointers) ...

    mSampleRate = GetSampleRate();
    mDesiredBitClockPeriod = double(mSampleRate) / double(mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate);

    // SRAT-01: Sample rate advisory — emit before first decoded slot
    constexpr U32 kMinOversampleRatio = 4;
    U64 bit_clock_hz = U64( mSettings->mTdmFrameRate ) * U64( mSettings->mSlotsPerFrame ) * U64( mSettings->mBitsPerSlot );
    U64 recommended_min = bit_clock_hz * kMinOversampleRatio;
    bool low_sample_rate = ( mSampleRate < recommended_min );

    if( low_sample_rate )
    {
        char msg[ 256 ];
        // auto-scale units for display
        snprintf( msg, sizeof( msg ),
            "Capture rate: %s is below recommended 4x bit clock (%s). "
            "4x oversampling is needed for reliable edge detection.",
            FormatHz( mSampleRate ),
            FormatHz( recommended_min ) );

        FrameV2 advisory;
        advisory.AddString( "severity", "warning" );
        advisory.AddString( "message", msg );
        // Use sample 0 to mClock's current position as span
        // (or use GetTriggerSample() if available — see Open Questions)
        mResults->AddFrameV2( advisory, "advisory", 0, mClock->GetSampleNumber() );
        mResults->CommitResults();
    }

    // Store for AnalyzeTdmSlot() to use
    mLowSampleRate = low_sample_rate;

    SetupForGettingFirstBit();
    SetupForGettingFirstTdmFrame();
    mFrameNum = 0;

    for( ;; )
    {
        GetTdmFrame();
        CheckIfThreadShouldExit();
    }
}
```

### Pattern 3: low_sample_rate Boolean + Severity Elevation in AnalyzeTdmSlot()

**What:** Add `low_sample_rate` boolean field to every slot FrameV2 frame. When `mLowSampleRate` is true and the slot has no decode errors (severity would otherwise be "ok"), elevate severity to "warning".

**When to use:** Per CONTEXT.md — allows HLA scripts to detect low-sample-rate condition on individual slot frames without re-reading the advisory frame.

**Example:**

```cpp
// In AnalyzeTdmSlot(), in the FrameV2 block, after existing severity logic:

// Severity logic with low_sample_rate elevation:
const char* severity;
if( is_short_slot || is_bitclock_error || is_missed_data || is_missed_frame_sync )
    severity = "error";
else if( is_extra_slot || mLowSampleRate )
    severity = "warning";
else
    severity = "ok";

frame_v2.AddString( "severity", severity );
frame_v2.AddBoolean( "short_slot", is_short_slot );
frame_v2.AddBoolean( "extra_slot", is_extra_slot );
frame_v2.AddBoolean( "bitclock_error", is_bitclock_error );
frame_v2.AddBoolean( "missed_data", is_missed_data );
frame_v2.AddBoolean( "missed_frame_sync", is_missed_frame_sync );
frame_v2.AddBoolean( "low_sample_rate", mLowSampleRate );
// NOTE: low_sample_rate is emitted last — existing fields preserve position
```

**Field count:** The nine-field schema from Phase 5 becomes a ten-field schema with the addition of `low_sample_rate`. The boolean is emitted unconditionally on every slot frame.

### Pattern 4: Unit Auto-Scaling Helper

**What:** A small helper function or inline lambda that converts Hz values to human-readable strings (Hz, kHz, MHz).

**When to use:** For both the advisory message (SRAT-01) and the settings error message (SRAT-02).

**Example (as a static function or standalone helper):**

```cpp
// Source: Claude's discretion — CONTEXT.md specifies "auto-scale units"

static void FormatHzString( char* buf, size_t buf_size, U64 hz )
{
    if( hz >= 1000000ULL )
        snprintf( buf, buf_size, "%llu MHz", (unsigned long long)(hz / 1000000ULL) );
    else if( hz >= 1000ULL )
        snprintf( buf, buf_size, "%llu kHz", (unsigned long long)(hz / 1000ULL) );
    else
        snprintf( buf, buf_size, "%llu Hz", (unsigned long long)hz );
}
```

This function is called in both `TdmAnalyzer.cpp` (for the WorkerThread advisory) and `TdmAnalyzerSettings.cpp` (for the SetSettingsFromInterfaces error message). Placement options:

- As a `static` free function in each `.cpp` file (duplicated but simple, avoids header change)
- As a `static` member of `TdmAnalyzerSettings` (one definition, accessible from both files via forward declaration)
- As a free function in a small `TdmAnalyzerHelpers.h` (cleanest but adds a file)

Given the project pattern (no helper files), a `static` free function at file scope in each `.cpp` is the simplest approach.

### Anti-Patterns to Avoid

- **Using `SetErrorText()` for the soft advisory:** `SetErrorText()` requires returning `false` from `SetSettingsFromInterfaces()`, which blocks analysis entirely. The soft advisory must go in `WorkerThread()` via FrameV2. (Pitfall 7 from PITFALLS.md)

- **Calling `GetSampleRate()` from `SetSettingsFromInterfaces()`:** Not available in that context — only callable from `WorkerThread()`. The hard block (SRAT-02) uses only settings parameters (no runtime sample rate). (Pitfall 8 from PITFALLS.md)

- **Using 4× oversampled rate (not raw bit clock) for the 500 MHz hard block:** The CONTEXT.md decision specifies the hard block is based on raw bit clock rate (`frame_rate × slots × bits_per_slot`), not the 4× oversampled minimum capture rate. The 500 MHz cap is the maximum Logic 2 hardware sample rate, but the bit clock is what the device must capture — the check should verify `bit_clock > 500 MHz`, not `4 * bit_clock > 500 MHz`.

- **Placing advisory FrameV2 after the main loop starts:** The advisory must appear before the first decoded slot (row 0). It must be emitted and `CommitResults()` called before `SetupForGettingFirstBit()` and the main loop.

- **Omitting `low_sample_rate` field from slot frames:** The CONTEXT.md decision requires this boolean on every slot frame. Omitting it creates sparse columns in the Logic 2 data table and breaks HLA scripts that rely on unconditional field presence (established pattern from Phase 5).

- **Forgetting `mLowSampleRate` member variable in TdmAnalyzer.h:** The `WorkerThread()` computes `low_sample_rate` once and stores it. `AnalyzeTdmSlot()` is called many times from `GetTdmFrame()`. The value must be stored as a member so `AnalyzeTdmSlot()` can access it without recomputing each call.

- **Integer overflow in bit clock calculation:** `mSlotsPerFrame` (max 256) × `mBitsPerSlot` (max 64) × `mTdmFrameRate` (user U32, could be large) can overflow a U32. Use U64 casts on all three operands before multiplying. The existing `GetMinimumSampleRateHz()` returns U32 and is vulnerable to this — do not copy its pattern for the new checks.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Sample rate comparison threshold | Custom threshold formula | `4 * bit_clock_hz` (same formula as `GetMinimumSampleRateHz()`) | Consistency — diverging from `GetMinimumSampleRateHz()` creates two sources of truth |
| Unit formatting for Hz/kHz/MHz | Complex string formatting library | Simple `snprintf` with if/else for kHz/MHz thresholds | No external deps; pattern matches existing `snprintf` usage in codebase |
| Advisory frame ID/span | Complex sample position tracking | Use `0` to `mClock->GetSampleNumber()` at start of WorkerThread | Simple, spans the initial channel sync period, does not interfere with slot frames |

**Key insight:** This phase is pure application logic over existing SDK primitives. The SDK provides everything needed: `FrameV2::AddString`, `FrameV2::AddBoolean`, `AddFrameV2`, `SetErrorText`, `GetSampleRate`. No new patterns or libraries.

---

## Common Pitfalls

### Pitfall 1: Advisory frame sample span overlap with first slot frame
**What goes wrong:** `AddFrameV2()` requires that frames do not overlap in sample position. If the advisory frame's `ending_sample` equals or exceeds the first slot frame's `starting_sample`, Logic 2 may reject or silently drop one of the frames.
**Why it happens:** Using `mClock->GetSampleNumber()` for the advisory ending position may be past the first bit that `AnalyzeTdmSlot()` will use.
**How to avoid:** Use `starting_sample = 0` and `ending_sample = mClock->GetSampleNumber() - 1` for the advisory frame, or use `GetTriggerSample()` as a reference. The main loop starts at `SetupForGettingFirstBit()` which advances the clock — the advisory span ends before the first valid clock edge is consumed.
**Warning signs:** Data table shows no slot rows after the advisory row, or Logic 2 crashes during analysis.

### Pitfall 2: mLowSampleRate uninitialized before first AnalyzeTdmSlot() call
**What goes wrong:** If `mLowSampleRate` is declared in `TdmAnalyzer.h` but not initialized in the constructor, it contains garbage on the first analysis run (before `WorkerThread()` sets it). If a new analysis run is triggered without resetting `mLowSampleRate`, it may carry over from the previous run.
**Why it happens:** `WorkerThread()` is called every analysis run, so `mLowSampleRate` is always set before use within a single run — but only if the initialization in `WorkerThread()` is reached before `AnalyzeTdmSlot()` is called.
**How to avoid:** Initialize `mLowSampleRate = false` in `TdmAnalyzer::TdmAnalyzer()` constructor (alongside the other member initializers). The `WorkerThread()` will overwrite it at run start.

### Pitfall 3: Zero-parameter guard order in SetSettingsFromInterfaces()
**What goes wrong:** If the zero-parameter checks run after the bit clock calculation, dividing by or multiplying by zero produces undefined behavior (or wraps to a misleadingly large value for `U64 bit_clock_hz`).
**Why it happens:** The CONTEXT.md decision says zero-parameter checks go before existing validations and before the 500 MHz check. If the code layout puts the 500 MHz check block first (as a logical "big" check), then the zero checks are easy to misplace.
**How to avoid:** Follow the CONTEXT.md order strictly: (1) extract parameters from interfaces, (2) zero-parameter guard, (3) existing validations (data_bits <= bits_per_slot), (4) 500 MHz bit clock check. Add comments marking each block.

### Pitfall 4: SimpleArchive append-only rule — no new settings field is needed
**What goes wrong:** Adding a new settings field for sample rate behavior would require version-gating the `LoadSettings()`/`SaveSettings()` archive (see PITFALLS.md Pitfall 6). However, this phase does not add any persistent settings — both the advisory and the hard block are computed from existing settings at runtime.
**Why it happens:** Over-engineering — a developer might add a `mShowSampleRateWarning` checkbox or a `mMaxBitClockHz` settings field. Neither is needed per the CONTEXT.md decisions.
**How to avoid:** No new settings fields. The thresholds are named constants in the code, not user-configurable. No `LoadSettings()`/`SaveSettings()` changes needed.

### Pitfall 5: Advisory FrameV2 CommitResults() must precede main loop
**What goes wrong:** If `CommitResults()` is called only after the first slot is decoded (e.g., inside `AnalyzeTdmSlot()`), the advisory row may be buffered and appear in the wrong row order in the Logic 2 data table.
**Why it happens:** `CommitResults()` is normally called at the end of `AnalyzeTdmSlot()`. Emitting an advisory without an explicit commit before the loop starts could delay its appearance.
**How to avoid:** Call `mResults->CommitResults()` immediately after `mResults->AddFrameV2(advisory, ...)` in the advisory block within `WorkerThread()`, before the main loop starts.

---

## Code Examples

Verified patterns from codebase and SDK research:

### Existing: GetSampleRate() usage in WorkerThread()
```cpp
// Source: src/TdmAnalyzer.cpp, WorkerThread(), line 41 (confirmed in codebase)
mSampleRate = GetSampleRate();
mDesiredBitClockPeriod = double(mSampleRate) / double(mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate);
```

### Existing: SetErrorText() pattern in SetSettingsFromInterfaces()
```cpp
// Source: src/TdmAnalyzerSettings.cpp, SetSettingsFromInterfaces()
if( clock_channel == UNDEFINED_CHANNEL )
{
    SetErrorText( "Please select a channel for TDM CLOCK signal" );
    return false;
}
```

### Existing: FrameV2 emission + CommitResults() pattern
```cpp
// Source: src/TdmAnalyzer.cpp, AnalyzeTdmSlot()
FrameV2 frame_v2;
frame_v2.AddInteger( "slot", mResultsFrame.mType );
// ... more AddX() calls ...
mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
mResults->CommitResults();
```

### Existing: U64 member mSampleRate
```cpp
// Source: src/TdmAnalyzer.h, line 37 (confirmed in codebase)
U64 mSampleRate;
// This is already U64 — safe to compare against U64 bit_clock_hz
```

### New: GetMinimumSampleRateHz() for reference
```cpp
// Source: src/TdmAnalyzer.cpp, lines 354-358 (confirmed in codebase)
U32 TdmAnalyzer::GetMinimumSampleRateHz()
{
    return mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate * 4;
}
// NOTE: This uses U32 arithmetic — overflows at high frame rates
// The new bit clock calculations must use U64 throughout
```

### New: Advisory FrameV2 type string (Claude's discretion)
```cpp
// The type string passed to AddFrameV2() defines the "row type" in Logic 2.
// "advisory" is distinct from "slot" and clearly communicates purpose.
mResults->AddFrameV2( advisory, "advisory", start_sample, end_sample );
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Logic 2 SDK warning via `GetMinimumSampleRateHz()` only | SDK warning + explicit FrameV2 advisory in data table | Phase 6 | Advisory is visible in data table even when Logic 2's UI warning is dismissed |
| No bit clock ceiling validation | Hard block in SetSettingsFromInterfaces() for > 500 MHz bit clock | Phase 6 | Prevents silent analysis failure on impossible configurations |
| Nine-field slot FrameV2 schema (Phase 5) | Ten-field slot FrameV2 schema (adds `low_sample_rate`) | Phase 6 | HLA scripts can detect low-sample-rate condition per slot frame |

**Deprecated/outdated:**
- Relying solely on `GetMinimumSampleRateHz()`: Logic 2's advisory warning from this method is displayed in the settings panel but disappears once analysis starts. The new FrameV2 advisory persists in the data table.

---

## Open Questions

1. **Advisory frame sample span — exact end sample**
   - What we know: The advisory frame must precede the first slot frame. `WorkerThread()` calls `SetupForGettingFirstBit()` then `SetupForGettingFirstTdmFrame()` before the main loop. These functions advance `mClock`.
   - What's unclear: The exact sample number at `WorkerThread()` start (before any clock advancement) is whatever the channel data starts at, which may be 0 or some initial offset.
   - Recommendation: Use `starting_sample = 0` and `ending_sample = 0` for the advisory (a zero-duration point frame at sample 0), which is safe — it precedes all bit clock edges. Alternatively, after `GetAnalyzerChannelData()` and before `SetupForGettingFirstBit()`, capture `mClock->GetSampleNumber()` as the end sample.

2. **Low_sample_rate severity interaction with existing error flags**
   - What we know: CONTEXT.md says "when `low_sample_rate` is true and no decode errors are present, slot severity is elevated to 'warning' (not 'ok')". The current severity logic is: error-wins if short_slot/bitclock_error/missed_data/missed_frame_sync; else warning if extra_slot; else ok.
   - What's unclear: Should `low_sample_rate` elevate an existing "ok" to "warning" only, or also affect "error" slots? CONTEXT.md says "no decode errors are present" — this means the elevation only changes "ok" → "warning", not "error" → anything.
   - Recommendation: The severity if/else chain: `error` for decode errors → `warning` for extra_slot OR low_sample_rate (when no decode errors) → `ok` otherwise. This matches the CONTEXT.md description. An error slot with `low_sample_rate = true` stays "error".

3. **FormatHz helper placement**
   - What we know: The helper is needed in both `TdmAnalyzerSettings.cpp` and `TdmAnalyzer.cpp`.
   - What's unclear: Whether to duplicate as a static function in each file or introduce a shared location.
   - Recommendation: Duplicate as a file-scope `static` function in each `.cpp` file. The function is ~8 lines; duplication is justified over adding a new header/source file to the project.

---

## Implementation Summary

### Files to Modify

| File | Change | Requirement |
|------|--------|-------------|
| `src/TdmAnalyzerSettings.h` | Add named constants `kMaxBitClockHz` and `kMinOversampleRatio` (as `constexpr` in class or file scope) | SRAT-01, SRAT-02 |
| `src/TdmAnalyzerSettings.cpp` | Add zero-param guards + 500 MHz hard block in `SetSettingsFromInterfaces()` | SRAT-02 |
| `src/TdmAnalyzer.h` | Add `bool mLowSampleRate` member variable | SRAT-01 |
| `src/TdmAnalyzer.cpp` | (1) Advisory FrameV2 emission in `WorkerThread()` init; (2) `low_sample_rate` boolean + severity elevation in `AnalyzeTdmSlot()` | SRAT-01 |
| `CHANGELOG.md` | Document new `low_sample_rate` field (breaking: ten-field schema) + advisory frame type + settings validation changes | both |

### No New Files Needed

The phase is entirely incremental additions to existing files. No new source or header files.

### Suggested Task Split

**Task 1:** Settings validation (SRAT-02)
- Add `kMaxBitClockHz` and `kMinOversampleRatio` constants to `TdmAnalyzerSettings.h`
- Add zero-param guards in `SetSettingsFromInterfaces()`
- Add 500 MHz bit clock check with `SetErrorText()` + formatted message

**Task 2:** WorkerThread advisory + slot low_sample_rate (SRAT-01)
- Add `mLowSampleRate` member to `TdmAnalyzer.h`; initialize in constructor
- Add advisory FrameV2 emission block in `WorkerThread()` after `GetSampleRate()`
- Add `low_sample_rate` AddBoolean to `AnalyzeTdmSlot()` FrameV2 block
- Adjust severity logic to elevate "ok" → "warning" when `mLowSampleRate` is true

**Task 3:** CHANGELOG.md update

---

## Sources

### Primary (HIGH confidence)
- `src/TdmAnalyzer.cpp` — direct code inspection; confirmed `mSampleRate = GetSampleRate()` in WorkerThread(), `GetMinimumSampleRateHz()` formula, FrameV2 emission pattern with CommitResults()
- `src/TdmAnalyzerSettings.cpp` — direct code inspection; confirmed SetErrorText() pattern, parameter extraction order, existing validations
- `src/TdmAnalyzer.h` — direct code inspection; confirmed `U64 mSampleRate` member, no existing `mLowSampleRate` member
- `.planning/research/ARCHITECTURE.md` — Feature 4 documents GetSampleRate() constraint (settings context vs WorkerThread), 500 MHz threshold, U64 overflow risk
- `.planning/research/PITFALLS.md` — Pitfall 7 (SetErrorText blocks), Pitfall 8 (Nyquist threshold), Pitfall 6 (SimpleArchive append-only)
- `.planning/research/STACK.md` — Q4 confirms GetSampleRate() is NOT available in SetSettingsFromInterfaces(); only in WorkerThread()
- `.planning/phases/06-sample-rate-validation/06-CONTEXT.md` — all user decisions for this phase

### Secondary (MEDIUM confidence)
- `.planning/phases/05-framev2-enrichment/05-01-SUMMARY.md` — confirms Phase 5 established ten-field-capable FrameV2 pattern; AddBoolean unconditional emission; "All nine fields emitted unconditionally on every slot frame"

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — confirmed from direct source code inspection; no external dependencies needed
- Architecture: HIGH — two-file change with clear placement rules; both patterns already established in codebase
- Pitfalls: HIGH — derived from direct source code analysis + previous phase research which already documented these exact risks

**Research date:** 2026-02-25
**Valid until:** 2026-04-25 (stable — no external dependencies; SDK has not changed since July 2023)
