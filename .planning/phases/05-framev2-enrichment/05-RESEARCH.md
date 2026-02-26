# Phase 5: FrameV2 Enrichment - Research

**Researched:** 2026-02-25
**Domain:** Saleae Logic 2 FrameV2 API — C++ SDK field schema, boolean emission, severity string construction
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### Field schema (replaces current FrameV2 output)
- **Remove** `channel`, `errors`, and `warnings` fields entirely
- **Rename** `channel` to `slot` — same 0-based value, new key name
- **Add** five boolean fields: `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, `missed_frame_sync`
- **Add** `severity` string field with values: `error`, `warning`, or `ok`
- **Keep** `data`, `frame_number` fields unchanged

#### Column order (FrameV2 AddField sequence)
- `slot` → `data` → `frame_number` → `severity` → `short_slot` → `extra_slot` → `bitclock_error` → `missed_data` → `missed_frame_sync`
- Boolean columns ordered by expected frequency of occurrence
- All fields emitted on every slot row, including error-free rows (booleans show false, severity shows "ok")

#### Severity field logic
- Any error boolean true → severity = `error`
- Only `extra_slot` true (no errors) → severity = `warning`
- All booleans false → severity = `ok`
- Error wins: if both error and warning booleans are true, severity = `error`

#### Extra slot behavior
- `extra_slot` remains a warning (not promoted to error)
- Data is still decoded normally for extra slots — value is useful for debugging
- Slot number keeps incrementing beyond configured slots-per-frame (no cap/reset)
- Row coloring: DISPLAY_AS_WARNING_FLAG for warning-only, DISPLAY_AS_ERROR_FLAG wins when errors present

#### Short slot behavior
- Data field stays 0 when `short_slot` is true (current behavior — no partial decode)
- `short_slot` is an error — severity = `error`, DISPLAY_AS_ERROR_FLAG set

#### Frame number
- Stays 0-based, key name `frame_number` unchanged
- Extra slots stay on the same frame_number as the last valid slot in that frame

#### FrameV1 (legacy)
- No changes to FrameV1 / mResultsFrame behavior — only FrameV2 output changes
- mResultsFrame.mType still carries 0-based slot number via mSlotNum
- mResultsFrame.mFlags still carries the existing flag bitmask

#### Row coloring
- Preserve existing DISPLAY_AS_ERROR_FLAG and DISPLAY_AS_WARNING_FLAG behavior on Frame-level flags
- Error flag takes precedence over warning flag (current behavior, unchanged)

#### Requirements updates (do during this phase)
- **Update FRM2-06**: Change from "add 1-based slot alongside channel" to "replace channel with 0-based slot"
- **Add FRM2-07**: "Replace errors/warnings string fields with severity enum field (error/warning/ok)"
- **Update Phase 5 success criteria**: Replace channel references with slot, add severity column, remove 1-based mention

#### Versioning and documentation
- No version bump per-phase — accumulate to milestone end
- Breaking changes documented in CHANGELOG.md only (no README FrameV2 schema update)
- No existing HLA scripts to migrate — breaking changes are safe

### Claude's Discretion
- Exact implementation of severity string construction
- Whether to use AddBoolean vs AddInteger(0/1) for boolean fields (SDK capability dependent)
- Test approach for verifying boolean field emission

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| FRM2-01 | Add boolean `short_slot` field to FrameV2 output for each decoded slot — enables HLA filtering without string parsing | `AddBoolean()` confirmed in SDK FrameV2 class; flag bit `SHORT_SLOT (1<<3)` already computed in `mResultsFrame.mFlags` |
| FRM2-02 | Add boolean `extra_slot` field to FrameV2 output for each decoded slot | `AddBoolean()` available; flag bit `UNEXPECTED_BITS (1<<1)` already computed |
| FRM2-03 | Add boolean `bitclock_error` field to FrameV2 output for each decoded slot | `AddBoolean()` available; flag bit `BITCLOCK_ERROR (1<<5)` already computed via advanced analysis |
| FRM2-04 | Add boolean `missed_data` field to FrameV2 output for each decoded slot | `AddBoolean()` available; flag bit `MISSED_DATA (1<<2)` already computed via advanced analysis |
| FRM2-05 | Add boolean `missed_frame_sync` field to FrameV2 output for each decoded slot | `AddBoolean()` available; flag bit `MISSED_FRAME_SYNC (1<<4)` already computed via advanced analysis |
| FRM2-06 | Replace `channel` field with 0-based `slot` field — cleaner key name, no redundant column | Simple key rename from `"channel"` to `"slot"` in `AddInteger()` call; value (`mResultsFrame.mType`) unchanged |
| FRM2-07 | Replace `errors`/`warnings` string fields with `severity` enum field (`error`/`warning`/`ok`) — structured severity without string parsing | Remove two `AddString()` calls; add one `AddString("severity", ...)` with value derived from flag bitmask logic |
</phase_requirements>

---

## Summary

Phase 5 is a focused schema replacement in a single function: `TdmAnalyzer::AnalyzeTdmSlot()` in `src/TdmAnalyzer.cpp`. The FrameV2 block near the end of that function (approximately lines 302–340) currently adds five fields: `channel` (integer), `data` (integer), `errors` (string), `warnings` (string), `frame_number` (integer). This phase replaces that block with nine fields in the decided order: `slot` (integer), `data` (integer), `frame_number` (integer), `severity` (string), `short_slot` (boolean), `extra_slot` (boolean), `bitclock_error` (boolean), `missed_data` (boolean), `missed_frame_sync` (boolean).

The critical enabler is that the SDK's `FrameV2` class already provides `AddBoolean(const char* key, bool value)` — confirmed from the actual SDK header at commit `114a3b8`. All five error conditions are already detected and stored in `mResultsFrame.mFlags` using the existing bitmask constants before the FrameV2 block is reached, so the boolean values require only bitmask reads of a value already computed. The `severity` string is deterministic from the same flags: it is `"error"` if any of `SHORT_SLOT | MISSED_DATA | MISSED_FRAME_SYNC | BITCLOCK_ERROR` are set, `"warning"` if only `UNEXPECTED_BITS` is set (and no errors), or `"ok"` if no flags are set.

No other source files require changes for this phase. FrameV1 output (`mResultsFrame`, frame flags, bubble text, tabular text, CSV export) is explicitly out of scope and must not be touched. The change is isolated, low-risk, and backward-compatible at the binary level (the `.so`/`.dll` interface with Logic 2 is unchanged — only the content of FrameV2 data fields changes).

**Primary recommendation:** Rewrite the FrameV2 block in `AnalyzeTdmSlot()` as a nine-field sequence using the existing flag constants, adding `AddBoolean()` calls for the five error fields and replacing the two string fields with one `severity` string. Verify in Logic 2 data table that all nine columns appear on all rows, including clean frames.

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Saleae AnalyzerSDK | `114a3b8` (Jul 2023, pinned) | `FrameV2` class with `AddBoolean()`, `AddInteger()`, `AddString()` | Only SDK supported by Logic 2; no alternative |
| C++11 | Fixed by SDK CMake | Implementation language | SDK mandates C++11; standard lib only |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Standard C string (`<cstring>`) | C++11 stdlib | `snprintf` for severity string | Already used in the function; keep same pattern |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `AddBoolean(key, value)` | `AddInteger(key, value ? 1 : 0)` | `AddBoolean` is the correct SDK type for boolean semantics; Logic 2 may display it differently in the data table (checkbox vs integer); use `AddBoolean` per SDK intent |
| Single `const char*` literal for severity | `snprintf` into char buffer | Both work; literal is slightly simpler since values are known constants — either approach is acceptable per Claude's discretion |

**Installation:** No new dependencies. SDK already fetched by CMake `FetchContent`.

---

## Architecture Patterns

### Recommended Project Structure

No structural changes needed. All changes are within:

```
src/
└── TdmAnalyzer.cpp     # AnalyzeTdmSlot() — only function that changes
```

### Pattern 1: FrameV2 Field Emission Block

**What:** All FrameV2 fields for a single slot, emitted in a fixed sequence before `AddFrameV2()` is called.

**When to use:** Every call to `AnalyzeTdmSlot()` — once per decoded slot regardless of error state.

**Current code (to be replaced):**

```cpp
// Source: src/TdmAnalyzer.cpp ~line 302
FrameV2 frame_v2;
char error_str[ 80 ] = "";
char warning_str[ 32 ] = "";

frame_v2.AddInteger( "channel", mResultsFrame.mType );
S64 adjusted_value = result;
if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
{
    adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
}
frame_v2.AddInteger( "data", adjusted_value );

// ... snprintf loop building error_str, warning_str ...

frame_v2.AddString("errors", error_str);
frame_v2.AddString("warnings", warning_str);
frame_v2.AddInteger("frame_number", mFrameNum);
mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
```

**Replacement pattern (nine-field schema):**

```cpp
// Source: SDK header AnalyzerResults.h — AddBoolean confirmed
// Pattern follows Saleae CAN analyzer conventions (lowercase underscore keys)
FrameV2 frame_v2;

frame_v2.AddInteger( "slot", mResultsFrame.mType );

S64 adjusted_value = result;
if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
{
    adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
}
frame_v2.AddInteger( "data", adjusted_value );

frame_v2.AddInteger( "frame_number", mFrameNum );

// Severity: error wins over warning
const char* severity;
bool is_short_slot      = ( mResultsFrame.mFlags & SHORT_SLOT       ) != 0;
bool is_extra_slot      = ( mResultsFrame.mFlags & UNEXPECTED_BITS  ) != 0;
bool is_bitclock_error  = ( mResultsFrame.mFlags & BITCLOCK_ERROR   ) != 0;
bool is_missed_data     = ( mResultsFrame.mFlags & MISSED_DATA      ) != 0;
bool is_missed_frame_sync = ( mResultsFrame.mFlags & MISSED_FRAME_SYNC ) != 0;

if( is_short_slot || is_bitclock_error || is_missed_data || is_missed_frame_sync )
    severity = "error";
else if( is_extra_slot )
    severity = "warning";
else
    severity = "ok";

frame_v2.AddString(  "severity",          severity         );
frame_v2.AddBoolean( "short_slot",        is_short_slot    );
frame_v2.AddBoolean( "extra_slot",        is_extra_slot    );
frame_v2.AddBoolean( "bitclock_error",    is_bitclock_error );
frame_v2.AddBoolean( "missed_data",       is_missed_data   );
frame_v2.AddBoolean( "missed_frame_sync", is_missed_frame_sync );

mResults->AddFrameV2( frame_v2, "slot",
    mResultsFrame.mStartingSampleInclusive,
    mResultsFrame.mEndingSampleInclusive );
```

### Pattern 2: Existing Flag Bitmask — Read-Only in FrameV2 Block

**What:** The five flag macros are already defined in `TdmAnalyzerResults.h` and computed into `mResultsFrame.mFlags` before the FrameV2 block runs.

**When to use:** All boolean derivations read from `mResultsFrame.mFlags` — do NOT re-derive from raw bit vectors.

**Flag map:**

```cpp
// Source: src/TdmAnalyzerResults.h
#define UNEXPECTED_BITS   ( 1 << 1 )  // → extra_slot
#define MISSED_DATA       ( 1 << 2 )  // → missed_data
#define SHORT_SLOT        ( 1 << 3 )  // → short_slot
#define MISSED_FRAME_SYNC ( 1 << 4 )  // → missed_frame_sync
#define BITCLOCK_ERROR    ( 1 << 5 )  // → bitclock_error
// Note: bits 6 (DISPLAY_AS_WARNING_FLAG) and 7 (DISPLAY_AS_ERROR_FLAG)
// are NOT mapped to boolean fields — they are display hints for FrameV1 only
```

### Anti-Patterns to Avoid

- **Conditional field emission:** Do not use `if (is_short_slot) { frame_v2.AddBoolean(...); }`. Every field must be emitted on every frame. Conditional emission creates sparse columns in the Logic 2 data table and breaks HLA attribute access.
- **Removing `error_str`/`warning_str` from FrameV1 code:** The `snprintf` loops in `GenerateBubbleText()` and `GenerateFrameTabularText()` in `TdmAnalyzerResults.cpp` are FrameV1 display code. Do NOT touch them. Only the FrameV2 block in `TdmAnalyzer.cpp::AnalyzeTdmSlot()` changes.
- **Renaming the `AddFrameV2` type string:** The second argument to `mResults->AddFrameV2(frame_v2, "slot", ...)` is the frame type label used by Logic 2 for row grouping. It is already `"slot"` in the current code and must remain `"slot"`.
- **Using `DISPLAY_AS_WARNING_FLAG` or `DISPLAY_AS_ERROR_FLAG` bits in FrameV2:** These flag bits drive FrameV1 row coloring and are correctly left in `mResultsFrame.mFlags`. Do not pass them as FrameV2 boolean fields.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Boolean field type | `AddInteger(key, value ? 1 : 0)` | `AddBoolean(key, value)` | SDK provides boolean type; Logic 2 may display it as a checkbox; use the right type |
| Severity string construction | Complex `std::string` concatenation or enum-to-string map | `const char*` literal assignment via if/else chain | Three fixed values, deterministic logic — string literal is correct and zero-allocation |
| Error flag detection | Re-scanning `mDataBits`/`mDataFlags` vectors | Read `mResultsFrame.mFlags` bitmask (already computed) | Flags are fully accumulated before the FrameV2 block runs; no need to re-derive |

**Key insight:** All data this phase needs is already computed. The work is schema replacement, not new detection logic.

---

## Common Pitfalls

### Pitfall 1: Conditional FrameV2 Field Emission Breaks Logic 2 Data Table

**What goes wrong:** Adding a FrameV2 field only when a condition is true (e.g., only adding `"short_slot"` when `is_short_slot == true`) creates sparse column data. Logic 2 shows blank cells for frames that did not emit the field. HLA scripts get `KeyError` or `AttributeError` when accessing the field on a clean frame.

**Why it happens:** Developers think "why emit `false` if there is nothing to report?" The SDK does not validate completeness; it accepts partial field sets silently.

**How to avoid:** Always emit all nine fields for every slot frame. If the slot is clean, booleans are `false` and severity is `"ok"`. This is the established Saleae pattern (see CAN analyzer reference).

**Warning signs:** Logic 2 data table shows some rows with empty cells in `short_slot` column while other rows show values.

### Pitfall 2: Touching FrameV1 Code in TdmAnalyzerResults.cpp

**What goes wrong:** The `snprintf` loops building `error_str` and `warning_str` appear in three places: `AnalyzeTdmSlot()` (FrameV2, will be removed), `GenerateBubbleText()` (FrameV1 bubbles, keep), and `GenerateFrameTabularText()` (FrameV1 tabular text, keep). Accidentally modifying the latter two breaks the protocol decoder's visual display in Logic 2.

**Why it happens:** All three locations look similar. Phase scope says "remove `errors`/`warnings` fields" which a developer might misread as "remove all error string formatting."

**How to avoid:** Changes are confined to `TdmAnalyzer.cpp::AnalyzeTdmSlot()` only. Do not open `TdmAnalyzerResults.cpp` for this phase except to verify nothing changed.

**Warning signs:** Logic 2 protocol bubbles show blank or incorrect error labels after the change.

### Pitfall 3: Severity Logic Inverted for Extra Slot

**What goes wrong:** `UNEXPECTED_BITS` (extra slot) maps to `DISPLAY_AS_WARNING_FLAG`, but if an extra slot also has an error condition (e.g., bitclock error on the extra slot's bits), the severity must be `"error"`, not `"warning"`. The condition is "error wins" — check all four error flags first; only fall through to warning if none are set.

**Why it happens:** Code written as `if (is_extra_slot) return "warning"` before checking error flags will return `"warning"` even when error flags are also set.

**How to avoid:** Check `is_short_slot || is_bitclock_error || is_missed_data || is_missed_frame_sync` first. Only check `is_extra_slot` in the `else if` branch.

**Warning signs:** A frame with `extra_slot=true` and `bitclock_error=true` shows `severity="warning"` instead of `severity="error"`.

### Pitfall 4: Removing the `error_str`/`warning_str` Char Buffers Prematurely in AnalyzeTdmSlot

**What goes wrong:** The current `AnalyzeTdmSlot()` declares `char error_str[80]` and `char warning_str[32]` and populates them via `snprintf` loops. After the phase change, these buffers are no longer needed in this function. If they are removed but the `snprintf` loops are left in place, the code will not compile. If the `snprintf` loops are removed but the buffers are kept, there is dead code but no functional issue.

**How to avoid:** Remove both the buffer declarations AND the `snprintf` loops that populate them in `AnalyzeTdmSlot()`. The buffers and loops in `GenerateBubbleText()` and `GenerateFrameTabularText()` in `TdmAnalyzerResults.cpp` are separate and must be left untouched.

### Pitfall 5: UseFrameV2() Is Already Present — Do Not Remove It

**What goes wrong:** If any SDK-template-style refactor during this phase accidentally removes the `UseFrameV2()` call in `TdmAnalyzer::TdmAnalyzer()`, all FrameV2 data is silently dropped. No crash, no compile error — just an empty data table.

**How to avoid:** Do not modify `TdmAnalyzer::TdmAnalyzer()`. The constructor is not in scope for this phase.

---

## Code Examples

Verified patterns from official sources and existing codebase:

### SDK FrameV2 Complete API

```cpp
// Source: saleae/AnalyzerSDK include/AnalyzerResults.h @ 114a3b8 (confirmed HIGH confidence)
class LOGICAPI FrameV2
{
  public:
    FrameV2();
    ~FrameV2();

    void AddString    ( const char* key, const char* value );
    void AddDouble    ( const char* key, double value );
    void AddInteger   ( const char* key, S64 value );
    void AddBoolean   ( const char* key, bool value );   // USE THIS for boolean fields
    void AddByte      ( const char* key, U8 value );
    void AddByteArray ( const char* key, const U8* data, U64 length );

    FrameV2Data* mInternals;
};

// In AnalyzerResults:
void AddFrameV2( const FrameV2& frame, const char* type,
                 U64 starting_sample, U64 ending_sample );
```

### Existing FrameV2 Block (Current State — to be replaced)

```cpp
// Source: src/TdmAnalyzer.cpp AnalyzeTdmSlot() current state
FrameV2 frame_v2;
char error_str[ 80 ] = "";
char warning_str[ 32 ] = "";

frame_v2.AddInteger( "channel", mResultsFrame.mType );  // → rename to "slot"
S64 adjusted_value = result;
if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
    adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
frame_v2.AddInteger( "data", adjusted_value );

// snprintf loop building error_str, warning_str...

frame_v2.AddString("errors", error_str);      // → REMOVE
frame_v2.AddString("warnings", warning_str);  // → REMOVE
frame_v2.AddInteger("frame_number", mFrameNum);
mResults->AddFrameV2( frame_v2, "slot", ... );
```

### Existing Flag Constants (Unchanged — read-only input)

```cpp
// Source: src/TdmAnalyzerResults.h (unchanged this phase)
#define UNEXPECTED_BITS   ( 1 << 1 )   // extra_slot
#define MISSED_DATA       ( 1 << 2 )   // missed_data
#define SHORT_SLOT        ( 1 << 3 )   // short_slot
#define MISSED_FRAME_SYNC ( 1 << 4 )   // missed_frame_sync
#define BITCLOCK_ERROR    ( 1 << 5 )   // bitclock_error
// Set in: AnalyzeTdmSlot() (UNEXPECTED_BITS, SHORT_SLOT)
//         GetNextBit() advanced analysis (BITCLOCK_ERROR, MISSED_DATA, MISSED_FRAME_SYNC)
// Read in: FrameV2 block (this phase writes against these)
```

### Saleae CAN Analyzer — Reference Naming Convention

```cpp
// Source: github.com/saleae/can-analyzer/blob/master/src/CanAnalyzer.cpp (HIGH confidence)
// Confirmed pattern: lowercase underscore keys, boolean fields, always-emit-all
frame_v2.AddBoolean( "remote_frame", is_remote_frame );
frame_v2.AddBoolean( "standard_identifier", is_standard_identifier );
frame_v2.AddInteger( "identifier", identifier );
frame_v2.AddInteger( "num_data_bytes", num_data_bytes );
// → All fields emitted every frame; no conditional emission
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| String-encoded error fields (`errors`, `warnings`) | Boolean per-error fields + `severity` enum | Phase 5 (now) | HLA scripts can filter programmatically without string parsing |
| `channel` key (integer, 0-based) | `slot` key (integer, 0-based) | Phase 5 (now) | Consistent naming; no value change |
| `"frame #"` key (space + hash) | `"frame_number"` key | Phase 4 (done) | Already fixed; this phase preserves the corrected key |

**Deprecated/outdated after this phase:**
- `errors` field: removed; replaced by five boolean fields
- `warnings` field: removed; replaced by `severity` + `extra_slot` boolean
- `channel` field: replaced by `slot` (same value, renamed key)

---

## Open Questions

1. **`AddBoolean` display in Logic 2 data table**
   - What we know: `AddBoolean()` is in the SDK FrameV2 class API (confirmed from header); Logic 2 2.4.40 is current
   - What's unclear: Whether Logic 2 renders boolean fields as `true`/`false` strings, checkboxes, or `0`/`1` in the data table. No direct verification without running Logic 2.
   - Recommendation: Proceed with `AddBoolean()` as the correct SDK type. If Logic 2 renders them as `0`/`1`, that is still machine-readable for HLA. HLA Python access `frame.data["short_slot"] == True` should work regardless of display format since the value type is boolean in the SDK.

2. **Column ordering in Logic 2 data table**
   - What we know: The column order in the Logic 2 data table matches the `AddField` call sequence in the FrameV2 block (established convention from Phase 4 experience with `frame_number`)
   - What's unclear: Whether Logic 2 has any fixed/pinned columns that override the sequence order
   - Recommendation: Emit fields in the decided order (`slot` → `data` → `frame_number` → `severity` → booleans). If Logic 2 reorders, it is a display cosmetic issue, not a correctness issue.

---

## Sources

### Primary (HIGH confidence)
- `src/TdmAnalyzer.cpp` — current FrameV2 block in `AnalyzeTdmSlot()`, lines ~302–340; all flag constants and bitmask computation confirmed in source
- `src/TdmAnalyzerResults.h` — flag macro definitions (`SHORT_SLOT`, `UNEXPECTED_BITS`, etc.)
- `.planning/research/STACK.md` — FrameV2 full class API reproduced from `saleae/AnalyzerSDK include/AnalyzerResults.h @ 114a3b8`, including `AddBoolean()` signature; CAN analyzer reference confirming always-emit pattern

### Secondary (MEDIUM confidence)
- `.planning/research/PITFALLS.md` — Pitfall 5 (conditional fields create sparse columns), Pitfall 4 (`UseFrameV2()` must not be removed) — derived from SDK behavior analysis and cross-reference with Saleae community discussion

### Tertiary (LOW confidence)
- None — all claims for this phase are grounded in direct source inspection or confirmed SDK header inspection.

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — SDK header confirmed at pinned commit; `AddBoolean()` signature verified
- Architecture: HIGH — change is isolated to one function in one file; flag values and existing infrastructure verified in source
- Pitfalls: HIGH — derived from existing codebase patterns, SDK documentation, and prior phase research (PITFALLS.md)

**Research date:** 2026-02-25
**Valid until:** SDK is pinned to `114a3b8`; valid indefinitely unless SDK is updated (no update needed per Phase 4 finding). Logic 2 display behavior of `AddBoolean()` is the one uncertain area — low risk.
