# Phase 3: Code Quality and Documentation - Research

**Researched:** 2026-02-24
**Domain:** C++ modernization, WAV format limits, CMake FetchContent documentation
**Confidence:** HIGH

---

## Summary

Phase 3 is a straightforward cleanup phase with no external library dependencies or new
architecture. Every requirement maps directly to a specific, already-located code location.
The research task is less about discovering unknown patterns and more about confirming what
needs to change and how.

QUAL-01 is the easiest win: a grep for `auto_ptr` in the codebase finds **zero hits** in any
`.cpp` or `.h` file. Every smart pointer in the TDM analyzer is already `std::unique_ptr`.
QUAL-01 requires only confirming the absence of `auto_ptr` and closing the requirement.

QUAL-02 is the most substantive code change: the `TdmBitAlignment` enum uses the values
`BITS_SHIFTED_RIGHT_1` and `NO_SHIFT`. These names describe the symptom (shift amount) rather
than the semantic (DSP Mode A vs DSP Mode B). The UI tooltip already says "DSP mode A" and
"DSP mode B" — the enum values should match. The rename must propagate to all use sites in
`TdmAnalyzerSettings.cpp`, `TdmAnalyzerSettings.h`, `TdmAnalyzer.cpp`, and any test files.

QUAL-03 adds a pre-export warning gate in `GenerateWAV()`. The WAV RIFF chunk size field is a
`U32`, which caps the data payload at 4,294,967,296 bytes (4 GiB − 1). The computation is:
`total_bytes = num_frames × bytes_per_frame`. If this exceeds `UINT32_MAX − 36` (subtracting
the fixed PCM header overhead before the data chunk), the export would silently corrupt the
RIFF size field. The warning needs to be user-visible; the Saleae SDK provides
`SetErrorText()` and `AnalyzerHelpers::Assert()` but those are settings-phase APIs. Inside
`GenerateWAV()` / `GenerateExportFile()` the only output channel is the file and the progress
callback. The correct pattern is to compute the expected size before the export loop and
return early (writing nothing or writing a truncated marker) while logging via a comment or
the export progress mechanism. Because the SDK does not provide a blocking dialog API for
export callbacks, the practical pattern is to write a sentinel/empty file or simply not open
the file, and to rely on the progress system reaching 100% without any data.

DOCS-01 rewrites the README build section to explain intent, not just commands. The build
section currently lists bare CMake commands for macOS, Ubuntu, and Windows with no prose
explaining what FetchContent does, what the build directory is, or why there are separate
debug/release invocations. A developer unfamiliar with CMake FetchContent needs to understand:
(1) `cmake -S . -B build` configures the project and downloads the AnalyzerSDK via
FetchContent, (2) `cmake --build build` compiles the shared library, (3) the output lands in
`build/Analyzers/`. The Debug/Release distinction and the AppImage debugging instructions are
also worth brief explanation.

DOCS-02 removes the hedging language from the WAV export section. The current README says
"There is a bug in Logic 2 where the displayed export options are limited to TXT/CSV" and "when
this analyzer was authored" — framing that implies the bug might be fixed in a future Logic 2
version. Research in STACK.md (phase 1 pre-research) confirmed as of Logic 2.4.40 (December
2025) the custom export option API is still not honored by Logic 2 and Saleae has confirmed no
plans to implement it. The README should be rewritten to state this is the permanent design,
not a workaround for a bug under investigation.

**Primary recommendation:** All five requirements are small, targeted edits to specific files.
No new dependencies, no new architecture, no external library lookups needed. This phase can
be executed in a single plan covering all five requirements.

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| QUAL-01 | Migrate `std::auto_ptr` to `std::unique_ptr` if present | Grep confirms zero `auto_ptr` hits in src/; requirement is close-and-confirm, not a code change |
| QUAL-02 | Rename misleading `TdmBitAlignment` enum values to match DSP Mode A/B semantics | `BITS_SHIFTED_RIGHT_1` → `DSP_MODE_A`, `NO_SHIFT` → `DSP_MODE_B`; rename propagates to 3 files |
| QUAL-03 | Add WAV 4GB overflow pre-export warning | Compute `num_frames × frame_size_bytes` before export loop; warn/abort if > `UINT32_MAX - 36` |
| DOCS-01 | Clean up README build instructions to explain what each step does | Rewrite Linux build section prose to explain CMake, FetchContent, output location |
| DOCS-02 | Update WAV export documentation to state TXT/CSV workaround is permanent | Remove "bug" and "when this analyzer was authored" framing; state permanent architecture |
</phase_requirements>

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C++11 stdlib | C++11 | `std::unique_ptr`, `std::numeric_limits` | SDK mandates C++11; all modern smart pointer patterns are stdlib |
| Saleae AnalyzerSDK | 114a3b8 | Base classes used by export and settings APIs | Already in use; no changes to SDK interface |

### Supporting

No external libraries required. All changes are in-source edits and README prose.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Pre-export size computation | Streamed overflow detection mid-export | Pre-export is better UX; mid-export detection would produce a partial/corrupt file |
| Inline renamed enum values | Type alias / constexpr constants | Direct rename is cleaner; aliases would leave the old names alive and complicate the codebase |

---

## Architecture Patterns

### QUAL-01: Confirming auto_ptr Absence

**What:** Grep for `auto_ptr` in all `.cpp` and `.h` files under `src/`.

**Finding:** Zero hits. Every smart pointer in the TDM analyzer already uses `std::unique_ptr`.
Confirmed locations:
- `src/TdmAnalyzerSettings.h` — all interface members are `std::unique_ptr<...>`
- `src/TdmAnalyzer.h` — `mSettings` and `mResults` are `std::unique_ptr<...>`

**Execution:** Grep to confirm, document the finding, mark requirement complete. No code
change needed.

### QUAL-02: TdmBitAlignment Enum Rename

**Current state in `src/TdmAnalyzerSettings.h`:**
```cpp
enum TdmBitAlignment
{
    BITS_SHIFTED_RIGHT_1,
    NO_SHIFT
};
```

**UI label in `src/TdmAnalyzerSettings.cpp` (line 102-103):**
```cpp
mBitAlignmentInterface->AddNumber( BITS_SHIFTED_RIGHT_1, "Right-shifted by one (TDM typical, DSP mode A)", "" );
mBitAlignmentInterface->AddNumber( NO_SHIFT, "No shift (DSP mode B)", "" );
```

**Recommended rename:**
```cpp
enum TdmBitAlignment
{
    DSP_MODE_A,   // data shifted right by 1 bit from frame sync; standard TDM/I2S
    DSP_MODE_B    // data starts on same clock edge as frame sync; no shift
};
```

**Use sites to update (all in src/):**

| File | Line(s) | Old Value | Action |
|------|---------|-----------|--------|
| `TdmAnalyzerSettings.h` | enum definition | `BITS_SHIFTED_RIGHT_1`, `NO_SHIFT` | Rename enum values |
| `TdmAnalyzerSettings.cpp` | `AddNumber()` calls, `SetNumber()`, `mBitAlignment` init | `BITS_SHIFTED_RIGHT_1`, `NO_SHIFT` | Update enum value references |
| `TdmAnalyzer.cpp` | `SetupForGettingFirstTdmFrame()` and `GetTdmFrame()` — two `if` conditions | `BITS_SHIFTED_RIGHT_1` | Update enum value references |

Confirmed `NO_SHIFT` only appears in the settings file (definition and AddNumber/SetNumber
calls). `BITS_SHIFTED_RIGHT_1` also appears in `TdmAnalyzer.cpp` in the bit-alignment
conditional.

**Comment in settings file (line 99) to update:**
```cpp
// enum TdmBitAlignment { FIRST_FRAME_BIT_BELONGS_TO_PREVIOUS_WORD, FIRST_FRAME_BIT_BELONGS_TO_CURRENT_WORD };
```
This stale comment reflects an even older naming; it should be removed or replaced with a
description of DSP Mode A/B semantics.

**Serialization impact:** The enum values are serialized/deserialized by integer position in
`LoadSettings()` / `SaveSettings()`. The rename changes the C++ symbol names but the integer
values (0 and 1) are unchanged, so serialized settings files remain compatible.

### QUAL-03: WAV 4GB Overflow Warning

**WAV format constraint:** The RIFF chunk size field (`mRiffCkSize`) is a `U32`. The maximum
representable value is `UINT32_MAX` = 4,294,967,295. The RIFF chunk size is `36 + data_size`
for standard PCM (36 = size of all header fields after the RIFF chunk header itself). So the
maximum data payload is `UINT32_MAX - 36` = 4,294,967,259 bytes.

**Compute before export:**
```cpp
void TdmAnalyzerResults::GenerateWAV( const char* file )
{
    U64 num_frames = GetNumFrames();
    U32 slots_per_frame = mSettings->mSlotsPerFrame;

    // Compute bytes per WAV frame (not TDM frame)
    U32 bytes_per_channel = /* determined by mDataBitsPerSlot, same logic as PCMWaveFileHandler ctor */;
    U32 frame_size_bytes = bytes_per_channel * slots_per_frame;

    // Count how many TDM frames have at least one valid slot (approximate; conservative check)
    // Simplest safe approximation: num_frames is the upper bound on output frames
    U64 estimated_data_bytes = (U64)num_frames * frame_size_bytes;
    constexpr U64 WAV_MAX_DATA_BYTES = (U64)UINT32_MAX - 36;

    if (estimated_data_bytes > WAV_MAX_DATA_BYTES)
    {
        // User-visible: write a sentinel or log to file header; SDK has no dialog API in export callback
        // Pattern: open file, write a text warning as first bytes, close — user sees non-WAV file
        // OR: do not open file at all, update progress to 100% immediately
        // Recommendation: write warning text to file so the result is visible, not silently empty
        std::ofstream f(file, std::ios::out);
        f << "ERROR: WAV export exceeds 4GB RIFF limit. "
          << "Estimated size: " << (estimated_data_bytes / (1024*1024*1024)) << " GiB. "
          << "Use TXT/CSV export instead and convert separately.\n";
        f.close();
        UpdateExportProgressAndCheckForCancel(num_frames, num_frames);
        return;
    }
    // ... rest of existing GenerateWAV logic
}
```

**Bytes-per-channel derivation:** The same `if/else` ladder from `PCMWaveFileHandler` ctor
should be extracted or duplicated. Since `PCMWaveFileHandler` is constructed after the check,
the check must reimplement the bit-depth-to-bytes-per-channel mapping inline, or
`PCMWaveFileHandler` can expose it as a static helper.

**Simpler upper-bound approach:** Use `mDataBitsPerSlot` to compute worst-case
`bytes_per_channel`:
- `bits_per_channel <= 8` → 1 byte
- `bits_per_channel <= 16` → 2 bytes
- `bits_per_channel <= 32` → 4 bytes
- `bits_per_channel <= 40` → 5 bytes
- `bits_per_channel <= 48` → 6 bytes
- `bits_per_channel <= 64` → 8 bytes

This logic already exists in `PCMWaveFileHandler::PCMWaveFileHandler()`; the pre-export check
can replicate it directly.

**Note on `num_frames` as upper bound:** `GetNumFrames()` returns the total count of decoded
frames (each frame is one TDM slot). Not all frames map to WAV output samples (extra slots
are skipped). The check uses `num_frames` as the worst-case upper bound — it may overestimate,
but conservative is correct for a warning.

### DOCS-01: README Build Section Rewrite

**Current state:** The README has separate build sections for macOS, Ubuntu 16.04, and Windows
with bare command sequences and no explanation of what each command does. No mention of
FetchContent, SDK download, or output location.

**Target state:** Each platform section should open with 1-2 sentences explaining what the
build process does overall (configures project, downloads AnalyzerSDK automatically via CMake
FetchContent, compiles a shared library). Then annotate or follow each command with a comment
explaining its purpose:

```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
# Configures the project: creates the build directory, downloads the AnalyzerSDK from GitHub
# (this requires an internet connection the first time), and generates the build files.
# CMAKE_BUILD_TYPE=Release enables compiler optimizations.

cmake --build build-release
# Compiles the analyzer. The output shared library (libtdm_analyzer.so / tdm_analyzer.dll)
# is placed in build-release/Analyzers/.
```

**FetchContent explanation:** The README should note that the first `cmake -S` invocation
downloads the Saleae AnalyzerSDK automatically; no manual SDK installation is needed.

**Output location note:** Add a sentence explaining where to find the `.so`/`.dll` and how to
load it into Logic 2 (point to the install instructions section that already exists).

**Scope:** Rewrite only the build command sections. Do not touch feature descriptions, error/
warning documentation, or install instructions (those are accurate).

### DOCS-02: WAV Export Documentation Update

**Current README text to remove/replace (lines 94-100 approximately):**
> "There is a bug in Logic 2 where the displayed export options are limited to `TXT/CSV`. This
> bug is still present in Logic v2.3.58 (when this analyzer was authored)."

**Target framing:**
> "Logic 2 does not support custom export types for Low Level Analyzers. The TXT/CSV export
> option is the only export path available to plugins. This is a confirmed Saleae design
> decision, not a temporary limitation. To export WAV audio, this analyzer repurposes the
> TXT/CSV export callback..."

**Remove:** All hedging phrases like "still present in Logic v2.3.58 (when this analyzer was
authored)" and any implication that a native WAV export option might appear in a future Logic 2
version.

**Add (or verify presence of):** A note that this workaround architecture is permanent and
that the analyzer is designed around it.

**Source for claim:** STACK.md documents that as of Logic 2.4.40 (December 2025), the Saleae
custom export API is still not honored, the feature request has 13 voters and remains Open,
and Saleae has confirmed on their forums that custom export is not supported in Logic 2.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| 4GB limit constant | Custom magic number | `UINT32_MAX` from `<cstdint>` or `<limits.h>` | Already available, self-documenting |
| Bytes-per-channel mapping | New helper class | Inline if/else (same as PCMWaveFileHandler ctor) | Minimal duplication; adding a helper class is over-engineering for 6 lines |
| User-visible warning in export | SDK dialog API (does not exist) | Write to the output file | No blocking dialog API exists in the export callback; file is the only output channel |

---

## Common Pitfalls

### Pitfall 1: Breaking Serialized Settings Compatibility
**What goes wrong:** Renaming enum values changes the integer backing if the enum order
changes, which would corrupt settings loaded from disk.
**Why it happens:** The settings are serialized as raw integers via `SimpleArchive`. If
`DSP_MODE_A` is given a different integer value than `BITS_SHIFTED_RIGHT_1` was (0), saved
settings files would load the wrong bit alignment.
**How to avoid:** Keep enum values in the same order — `DSP_MODE_A` first (= 0),
`DSP_MODE_B` second (= 1). The integer backing is identical; only the C++ symbol name changes.
**Verification:** Check `LoadSettings()` and `SaveSettings()` in `TdmAnalyzerSettings.cpp` —
they cast to/from `U32`, so value 0 → `DSP_MODE_A` and value 1 → `DSP_MODE_B` must hold.

### Pitfall 2: Missing a Use Site in the Enum Rename
**What goes wrong:** The compiler catches all compile-time references, but a renamed enum
value used via cast from integer (e.g., `TdmBitAlignment(bit_alignment)`) may not be caught
if it produces a valid but wrong value.
**How to avoid:** Use `git grep` / `grep -r` for both `BITS_SHIFTED_RIGHT_1` and `NO_SHIFT`
before and after the rename. Verify zero remaining hits after the change.

### Pitfall 3: 4GB Check Overcomplicates the WAV Handler
**What goes wrong:** Adding the pre-export check inside `PCMWaveFileHandler` couples the
warning to the class, making the class responsible for both writing and refusing to write.
**How to avoid:** Keep the check in `GenerateWAV()` before `PCMWaveFileHandler` is
constructed. The handler's job is to write; the caller's job is to validate preconditions.

### Pitfall 4: QUAL-01 Grep Missing Indirect Usage via Typedef
**What goes wrong:** `auto_ptr` might appear through a typedef or SDK header, not in the
TDM source itself.
**How to avoid:** The grep scope should include all `.h` and `.cpp` files in `src/`. The SDK
headers in the build directory are outside scope — they are pinned and the SampleAnalyzer
audit already removed `auto_ptr` from the SDK side.

### Pitfall 5: README Build Rewrites Accidentally Changing Behavior Claims
**What goes wrong:** While rewriting prose, accidentally modifying or deleting accurate
technical content (error/warning descriptions, flag values, install steps).
**How to avoid:** Scope edits to the "Building instructions" sections only (marked by `###
MacOS`, `### Ubuntu`, `### Windows` headers and their sub-sections). Leave all content above
and below those sections untouched.

---

## Code Examples

### QUAL-02: Enum rename pattern

Before (`src/TdmAnalyzerSettings.h`):
```cpp
enum TdmBitAlignment
{
    BITS_SHIFTED_RIGHT_1,
    NO_SHIFT
};
```

After:
```cpp
enum TdmBitAlignment
{
    DSP_MODE_A,  // data shifted right 1 bit from frame sync (TDM typical / I2S left-justified)
    DSP_MODE_B   // data starts on same clock edge as frame sync (no shift)
};
```

Before (`src/TdmAnalyzerSettings.cpp`):
```cpp
mBitAlignmentInterface->AddNumber( BITS_SHIFTED_RIGHT_1, "Right-shifted by one (TDM typical, DSP mode A)", "" );
mBitAlignmentInterface->AddNumber( NO_SHIFT, "No shift (DSP mode B)", "" );
mBitAlignmentInterface->SetNumber( mBitAlignment );
```

After (UI labels remain unchanged; only the enum values change):
```cpp
mBitAlignmentInterface->AddNumber( DSP_MODE_A, "Right-shifted by one (TDM typical, DSP mode A)", "" );
mBitAlignmentInterface->AddNumber( DSP_MODE_B, "No shift (DSP mode B)", "" );
mBitAlignmentInterface->SetNumber( mBitAlignment );
```

Before (`src/TdmAnalyzer.cpp`, appears in two functions):
```cpp
if( mSettings->mBitAlignment == BITS_SHIFTED_RIGHT_1 )
```

After:
```cpp
if( mSettings->mBitAlignment == DSP_MODE_A )
```

### QUAL-03: Pre-export size check

```cpp
void TdmAnalyzerResults::GenerateWAV( const char* file )
{
    U64 num_frames = GetNumFrames();

    // Compute bytes per channel to determine WAV frame size
    U32 bits = mSettings->mDataBitsPerSlot;
    U32 bytes_per_channel;
    if      (bits <=  8) bytes_per_channel = 1;
    else if (bits <= 16) bytes_per_channel = 2;
    else if (bits <= 32) bytes_per_channel = 4;
    else if (bits <= 40) bytes_per_channel = 5;
    else if (bits <= 48) bytes_per_channel = 6;
    else                 bytes_per_channel = 8;

    U64 frame_size_bytes = (U64)bytes_per_channel * mSettings->mSlotsPerFrame;

    // WAV RIFF data chunk size is a U32; max data payload is UINT32_MAX - 36 bytes.
    // Use num_frames as a conservative upper bound (actual output may be smaller if
    // extra slots are skipped, but this is the worst case).
    constexpr U64 WAV_MAX_DATA_BYTES = (U64)0xFFFFFFFF - 36ULL;
    U64 estimated_data_bytes = num_frames * frame_size_bytes;

    if (estimated_data_bytes > WAV_MAX_DATA_BYTES)
    {
        std::ofstream f;
        f.open(file, std::ios::out);
        if (f.is_open())
        {
            f << "WAV export aborted: estimated output ("
              << (estimated_data_bytes / (1024ULL * 1024 * 1024))
              << " GiB) exceeds the 4GB WAV RIFF format limit.\n"
              << "Export as TXT/CSV instead and convert to WAV with an external tool.\n";
            f.close();
        }
        UpdateExportProgressAndCheckForCancel(num_frames, num_frames);
        return;
    }

    // ... existing file open and PCMWaveFileHandler logic unchanged ...
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `std::auto_ptr` | `std::unique_ptr` | C++11 (deprecated), C++17 (removed) | Already done in this codebase; SampleAnalyzer updated Aug 2024 |
| Generic shift names (`BITS_SHIFTED_RIGHT_1`) | Protocol-semantic names (`DSP_MODE_A`) | C++ best practice: enums should name what a value means, not how it's implemented | Better readability; no behavior change |

---

## Open Questions

1. **QUAL-03: Should the warning be a text file or an empty file?**
   - What we know: Writing a text warning is more user-friendly than writing nothing (user
     understands why the export "failed"). Writing nothing/empty risks confusing the user.
   - What's unclear: Whether Logic 2 shows any UI feedback about an empty or unexpected export
     result vs. a text file with an error message.
   - Recommendation: Write the text warning file. It is the most informative option and uses
     only `std::ofstream`, which is already available in this file. A planner can decide
     whether a brief or verbose message is preferred.

2. **QUAL-01: Should the requirement be documented as "verified absent" or "verified and
   removed"?**
   - What we know: `auto_ptr` does not appear in the TDM analyzer source.
   - What's unclear: The requirement says "migrate if present" — it was written before anyone
     checked. The action may be a verification commit with a finding note rather than a code
     change commit.
   - Recommendation: A plan task should: (1) grep to confirm absence, (2) commit a note
     documenting the finding, (3) mark requirement complete. No code change is expected.

---

## Sources

### Primary (HIGH confidence)

- `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerSettings.h` — enum definitions,
  all interface members confirmed as `std::unique_ptr`
- `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerSettings.cpp` — AddNumber calls
  confirm "DSP mode A/B" are already the UI labels; enum value references located
- `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzer.cpp` — `BITS_SHIFTED_RIGHT_1`
  use sites in `SetupForGettingFirstTdmFrame()` and `GetTdmFrame()` confirmed
- `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerResults.h` — `WavePCMHeader`
  `mRiffCkSize` is `U32`; data payload limit derivation confirmed
- `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerResults.cpp` — `GenerateWAV()`
  and `PCMWaveFileHandler` ctor; bytes-per-channel mapping confirmed (lines 212-231)
- `/home/chris/gitrepos/saleae_tdm_analyer/README.md` — current build section content and
  WAV export description confirmed; lines 94-100 contain the hedged "bug" language
- `/home/chris/gitrepos/saleae_tdm_analyer/.planning/research/STACK.md` — Saleae custom
  export confirmed permanently broken as of Logic 2.4.40; feature request Open with 13 voters

### Secondary (MEDIUM confidence)

- WAV RIFF format spec: `U32 ckSize` in RIFF chunk header limits payload to ~4 GiB. This
  is standard knowledge; verified by the fact that the `WavePCMHeader` struct uses `U32
  mRiffCkSize` and `U32 mDataCkSize`.

### Tertiary (LOW confidence)

- None — all claims for this phase are verifiable from the codebase itself.

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no external libraries; all tools already in use
- Architecture: HIGH — all code locations confirmed by direct file inspection
- Pitfalls: HIGH — enum serialization and rename completeness are well-understood C++ patterns

**Research date:** 2026-02-24
**Valid until:** 2026-04-24 (stable C++ codebase; no external API changes expected)
