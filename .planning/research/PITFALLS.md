# Pitfalls Research

**Domain:** Saleae Logic 2 Protocol Analyzer Plugin — TDM/I2S Audio Decoding
**Researched:** 2026-02-23
**Confidence:** HIGH (grounded in direct source code inspection + verified against WAV spec, GCC/Clang docs, and Saleae documentation)

---

## Critical Pitfalls

### Pitfall 1: Fixed-Size Error String Buffer Overflow via sprintf Concatenation

**What goes wrong:**
Multiple error flags (SHORT_SLOT, MISSED_DATA, MISSED_FRAME_SYNC, BITCLOCK_ERROR) are each appended to a fixed `char error_str[80]` buffer using `sprintf(error_str + strlen(error_str), ...)`. If all four errors trigger on the same slot, the combined string exceeds the buffer:
- "E: Short Slot " = 14 chars
- "E: Data Error " = 14 chars
- "E: Frame Sync Missed " = 21 chars
- "E: Bitclock Error " = 18 chars
- Total = 67 chars + null = 68 chars — fits barely today, but any change to error text or addition of a new error type overflows silently.

This pattern repeats in three separate locations: `TdmAnalyzer.cpp` lines 311–329, `TdmAnalyzerResults.cpp` lines 51–71, and lines 490–510.

**Why it happens:**
The author suppressed the MSVC C4996 warning that would have flagged `sprintf` as unsafe (pragma at line 8 of `TdmAnalyzerResults.cpp`, line 8 of `TdmAnalyzerSettings.cpp`), silencing the compiler's early warning signal. Without that warning, the unsafe sprintf pattern spread across the codebase.

**How to avoid:**
Replace all `sprintf` with `snprintf`, passing the remaining buffer size explicitly at each call site:
```cpp
// Before
sprintf(error_str + strlen(error_str), "E: Data Error ");

// After
size_t used = strlen(error_str);
snprintf(error_str + used, sizeof(error_str) - used, "E: Data Error ");
```
Or eliminate fixed buffers entirely by building error strings with `std::string` concatenation. Remove the `#pragma warning(disable: 4996)` suppressions and fix the underlying root cause rather than hiding the warning.

**Warning signs:**
- `#pragma warning(disable: 4996)` at the top of a source file
- `sprintf(buf + strlen(buf), ...)` chaining pattern
- Multiple error flag checks that all write to the same fixed buffer

**Phase to address:** Audit phase — this is a known bug (documented in CONCERNS.md) that must be fixed before adding any new error conditions.

---

### Pitfall 2: Export File Type Interface Initialized with Wrong Variable

**What goes wrong:**
In `TdmAnalyzerSettings.cpp` line 136, the export file type dropdown is initialized with the wrong variable:
```cpp
mExportFileTypeInterface->SetNumber( mEnableAdvancedAnalysis ); // BUG: should be mExportFileType
```
`mEnableAdvancedAnalysis` is a `bool` (defaults to `false` = 0). `mExportFileType` is an `ExportFileType` enum. The constructor sets both to 0 by coincidence, so the initial state appears correct — but this is accidental. If default values ever change, or if a user relies on the UI default reflecting the saved setting, the wrong variable being used means the export type display will reflect the advanced analysis state, not the actual export file type choice.

Note that `UpdateInterfacesFromSettings()` (line 191) does use the correct variable, so the bug only manifests during initial construction, not after loading saved settings.

**Why it happens:**
Copy-paste error when adding the export type interface; the pattern for each interface uses `SetNumber(m<SettingName>)` but the variable names were not updated carefully.

**How to avoid:**
Verify every `SetNumber` / `SetInteger` / `SetValue` / `SetChannel` call in the constructor maps to the correct member variable. Add a static assertion or review pass that checks constructor init against `UpdateInterfacesFromSettings()` for consistency.

**Warning signs:**
- UI shows wrong initial state after fresh installation
- Constructor `SetNumber` and `UpdateInterfacesFromSettings` `SetNumber` use different variable names for the same interface
- Type mismatch between interface value type and variable passed in (bool vs enum)

**Phase to address:** Audit phase — straightforward one-line fix; verify the correct variable name, then test that the UI reflects the correct default export type on fresh load.

---

### Pitfall 3: `#pragma scalar_storage_order` Is GCC-Only — Silent Silencing on Clang/MSVC

**What goes wrong:**
The WAV header structs in `TdmAnalyzerResults.h` (lines 45–65 and 67–94) use `#pragma scalar_storage_order little-endian` to enforce little-endian byte ordering. This pragma is a GCC extension. It is **not supported by Clang** (see LLVM issue #34641, open since 2017 with no implementation). On macOS, Apple uses Clang, not GCC. When Clang silently ignores this pragma, struct fields are written in the platform's native byte order — which on x86_64 and ARM64 is little-endian anyway, so the bug is masked in practice. However:

1. On a hypothetical big-endian host, the header would be written in the wrong byte order.
2. The intent of the code is not enforced by the compiler on macOS or Windows (MSVC also ignores it).
3. There are no `static_assert` checks to verify the struct is 44 bytes (standard PCM) or 80 bytes (extended) — so any accidental padding goes undetected.

**Why it happens:**
The pragma was added to document intent and potentially enforce it on GCC (Linux CI uses GCC-14). The author may not have known the pragma was non-portable.

**How to avoid:**
Add `static_assert(sizeof(WavePCMHeader) == 44, "WavePCMHeader must be 44 bytes")` and `static_assert(sizeof(WavePCMExtendedHeader) == 80, "WavePCMExtendedHeader must be 80 bytes")` immediately after each struct definition. These catch packing issues on all compilers regardless of pragma support. Document in a comment that `scalar_storage_order` is for GCC only and that the implementation relies on all supported targets being little-endian.

**Warning signs:**
- Struct definition uses `#pragma scalar_storage_order` without a corresponding `static_assert` on struct size
- No comment acknowledging the GCC-only nature of the pragma
- WAV files produced on macOS or Windows have subtly wrong headers (byte-swapped fields in specific metadata fields only)

**Phase to address:** Audit phase — add `static_assert` guards; this prevents silent regressions on any future cross-platform porting work.

---

### Pitfall 4: WAV File Size Silently Corrupts Beyond 4 GB (U32 Overflow)

**What goes wrong:**
The RIFF and data chunk size fields in `WavePCMHeader` are `U32` (32-bit unsigned), capping WAV file content at 4,294,967,295 bytes (~4 GB). Long captures at high sample rates and many channels will silently overflow these fields without any error. For example:
- 48 kHz, 256 channels, 32-bit depth = 48000 × 256 × 4 = ~47 MB/second
- 4 GB is exhausted in approximately 90 seconds of capture at this configuration

`updateFileSize()` in `TdmAnalyzerResults.cpp` (line 311) computes `data_size_bytes` as `U32` before passing it to `writeLittleEndianData`. If `mTotalFrames * mFrameSizeBytes > UINT32_MAX`, the computed size silently wraps to a small value, corrupting the header while leaving all audio data intact in the file (making it unreadable by standard players).

**Why it happens:**
The RIFF/WAV specification itself uses 32-bit chunk sizes. The code faithfully implements the spec without detecting when the spec's limits are exceeded. There is no early warning or pre-flight calculation before export begins.

**How to avoid:**
Before beginning WAV export, calculate the expected file size:
```cpp
U64 expected_bytes = U64(num_frames) * U64(mFrameSizeBytes);
if (expected_bytes > 0xFFFFFFFFULL) {
    // warn user or limit export
}
```
Add a hard check in `updateFileSize()` that asserts or truncates cleanly when the limit is reached. For very large exports, the WAV format should be replaced with RF64/BW64 which uses 64-bit chunk sizes.

**Warning signs:**
- Data chunk size in a produced WAV header is much smaller than actual file size
- WAV file reports "invalid header" in audio players despite file size being large
- No pre-export size validation in `GenerateWAV()`
- `U32 data_size_bytes` computed from potentially large `mTotalFrames * mFrameSizeBytes`

**Phase to address:** Audit phase — add pre-export validation; note this is LOW probability for typical embedded system captures but HIGH impact when it occurs.

---

## Moderate Pitfalls

### Pitfall 5: WAV Header Update Every 10 ms Creates Excessive Disk Seeks

**What goes wrong:**
`PCMWaveFileHandler::addSample()` calls `updateFileSize()` every `(mSampleRate / 100)` frames, i.e., every 10 ms of audio. `updateFileSize()` performs three `seekp()` + `write()` operations followed by a `seekp()` back. At 48 kHz this is 480 frames per update — manageable. At high sample rates (192 kHz) with many channels (256), the effective I/O rate becomes:
- Seek frequency: 100× per second
- Each seek: 3 file position changes + writes + restore

For a 10-minute capture this is 60,000 header rewrites. On spinning media or network storage, this causes measurable slowdowns. On SSDs, it contributes unnecessary write amplification.

**How to avoid:**
The defensive rationale (preserve data if export is cancelled) can be satisfied with less frequency. Increase the update interval to every 1000 ms (`mSampleRate * 1`) rather than 100 ms. Alternatively, only update `updateFileSize()` in the `close()` function and handle cancellation separately by calling it in the cancellation path of `GenerateWAV()`.

**Warning signs:**
- Export of long captures is noticeably slower than import
- Profiling shows `seekp` as a hot path during WAV export
- `mSampleRate / 100` magic number in `addSample()` — the divisor controls update frequency

**Phase to address:** Audit phase — easy improvement with clear benefit; the check at line 277 of `TdmAnalyzerResults.cpp`.

---

### Pitfall 6: Incomplete Frame Handling Produces Undefined WAV Audio for Short Slots

**What goes wrong:**
When a TDM frame ends before all expected slots are received (e.g., hardware glitch causes a truncated frame), the `SHORT_SLOT` flag is set on the incomplete slot, and `AnalyzeTdmSlot()` returns early with `result = 0`. This means the partial slot contributes a zero sample to the WAV file — silence for that slot in that frame. However, `mSampleData[]` in `PCMWaveFileHandler` accumulates samples per-channel sequentially. If a `SHORT_SLOT` occurs in the middle of a multi-channel frame, subsequent channel samples for that incomplete audio frame are misaligned by one slot position — writing the wrong channel's data to the wrong WAV channel for all subsequent frames.

**Why it happens:**
`GenerateWAV()` iterates by frame index and calls `addSample()` for each frame whose `mType < num_slots_per_frame`. When a short slot is encountered, `addSample()` is not called for that slot, but the `mSampleIndex` state in `PCMWaveFileHandler` loses track of which channel it's accumulating. The WAV handler has no notion of "skip this slot but preserve channel alignment".

**How to avoid:**
In `GenerateWAV()`, when a frame has the `SHORT_SLOT` flag set, call `addSample(0)` as a placeholder to preserve channel alignment. Alternatively, track frame number and slot number separately, and explicitly zero-fill any missing slots before writing. Add a comment documenting the alignment dependency.

**Warning signs:**
- WAV files from captures with any `SHORT_SLOT` error flags have audio that "shifts" — channels swap identity partway through
- Exported WAV sounds correct at the start but drifts to incorrect channel mapping after any error frame
- `GenerateWAV()` has no special handling for `SHORT_SLOT` flags — it simply skips those frames

**Phase to address:** Audit phase — this is a correctness bug in WAV export that directly affects output validity when the signal under test has any anomalies.

---

### Pitfall 7: `GIT_TAG master` in FetchContent Pins to an Unstable Branch

**What goes wrong:**
`cmake/ExternalAnalyzerSDK.cmake` line 17 fetches the Saleae AnalyzerSDK with `GIT_TAG master`. This means every fresh build fetches whatever is currently on the SDK's `master` branch. If Saleae pushes a breaking change to the SDK API (changed virtual function signatures, renamed types, removed exports), the plugin build will silently break on any machine that hasn't cached the SDK. CI builds are stable only because GitHub Actions runner caches are consistent — a developer starting fresh will get a different SDK version.

**Why it happens:**
Using `master` was a pragmatic shortcut — it always gets the "latest" SDK without requiring manual version bumps. The Saleae documentation mentions that tags are available for version pinning but does not enforce it.

**How to avoid:**
Pin to a specific SDK commit hash or tag:
```cmake
GIT_TAG  v2.3.58  # or the specific commit hash known to work
```
Document the pinned version in the repo CLAUDE.md and the CMakeLists.txt comment. When upgrading the SDK, do it deliberately with a dedicated commit and changelog entry. This also makes the `SDK API update check` work item in PROJECT.md actionable — first pin the current version, then evaluate what a version bump would change.

**Warning signs:**
- `GIT_TAG master` or `GIT_TAG main` in any FetchContent declaration for a third-party dependency
- Build failures that appear on CI after a period of no source changes
- SDK-related compile errors that cannot be reproduced consistently

**Phase to address:** Audit phase — a one-line change with large build reproducibility benefit.

---

### Pitfall 8: Settings Binary Format Has No Version — Future Field Additions Break Saved Projects

**What goes wrong:**
`TdmAnalyzerSettings::LoadSettings()` reads fields in a strict sequential order from `SimpleArchive`. The only version identifier is the magic string `"SaleaeTdmAnalyzer"`. If a new setting field is added (e.g., a sample rate validation warning threshold), it is appended at the end of the archive in `SaveSettings()`. A user who loads an old project file will get the new field's default value (because `text_archive >> new_field` will fail and fall through to the default). This is by design and documented — but there is no way to distinguish between "old format without this field" and "new format with a corrupt field". If a field is ever removed or reordered, LoadSettings will silently assign wrong values to subsequent fields with no error.

**Why it happens:**
`SimpleArchive` is a sequential stream — it does not support named fields or schema versioning. The Saleae SDK provides no higher-level settings persistence mechanism.

**How to avoid:**
Add a numeric version field immediately after the magic string:
```cpp
// SaveSettings
text_archive << "SaleaeTdmAnalyzer";
text_archive << U32(2); // settings version

// LoadSettings
U32 version = 1;
text_archive >> version;
if (version >= 2) { text_archive >> new_field; }
```
This is a minimal change that gives explicit control over migration behavior. Never reorder existing fields; only append. Document the versioning scheme in a comment block above `LoadSettings`.

**Warning signs:**
- `LoadSettings` has no version check after the magic string
- Fields are loaded without any documentation of their addition order or version
- Any future PR adding a new setting will silently break existing user projects if it ever reorders fields

**Phase to address:** Audit phase — add version identifier now before any new settings fields are introduced.

---

### Pitfall 9: `TdmBitAlignment` Enum Name Is Misleading — BITS_SHIFTED_RIGHT_1 Is DSP Mode A

**What goes wrong:**
The enum value `BITS_SHIFTED_RIGHT_1` in `TdmAnalyzerSettings.h` line 14 is described in the UI as "Right-shifted by one (TDM typical, DSP mode A)". The implementation comment at `TdmAnalyzerSettings.cpp` line 104 and the internal variable name `BITS_SHIFTED_RIGHT_1` disagree with common industry terminology. New maintainers reading the code will mentally associate "shifted right by one" with bit-level shifting operations rather than frame sync timing — causing confusion when modifying the frame alignment logic in `GetTdmFrame()` and `SetupForGettingFirstTdmFrame()`.

**How to avoid:**
Rename the enum values to match industry terminology:
```cpp
enum TdmBitAlignment {
    DSP_MODE_A = 0,   // was BITS_SHIFTED_RIGHT_1: first bit after FS pulse belongs to current frame
    DSP_MODE_B = 1,   // was NO_SHIFT: first bit of FS pulse belongs to current frame
};
```
Update all call sites. Add a comment documenting the physical behavior (where the first data bit appears relative to the frame sync edge) alongside the enum definition.

**Warning signs:**
- Comments alongside enum values explain what the name doesn't convey
- Multiple places in the codebase add clarifying comments about `BITS_SHIFTED_RIGHT_1`
- New contributors ask "which mode is which?" in code review

**Phase to address:** Audit phase — rename before adding any new alignment modes to avoid compounding the confusion.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| `GIT_TAG master` for SDK FetchContent | Always gets latest SDK without manual version bumps | Build non-reproducibility; CI breaks on SDK API changes | Never — always pin to a specific tag or commit |
| `sprintf` with fixed 80-char buffer for error strings | Simple to write, no allocation overhead | Buffer overflow risk if error messages grow; warning suppressed with pragma | Never — replace with `snprintf` or `std::string` |
| `#pragma scalar_storage_order` without `static_assert` on struct size | Expresses endianness intent | Silently ignored by Clang/MSVC; no compile-time verification that struct is correctly packed | Acceptable only if paired with `static_assert(sizeof(struct) == N)` |
| Header update every 10ms in WAV export | Defensive: preserves data on cancel | Excessive seeks degrade performance on long captures | Acceptable only during early development; should be tuned |
| Export type routing via settings field instead of `file_type` parameter | Works around Logic 2 bug | Fragile: export behavior depends on internal settings state, not the export request | Acceptable as a documented workaround while the SDK bug exists |

---

## Integration Gotchas

Common mistakes when connecting to the Saleae SDK.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| AnalyzerSDK FetchContent | `GIT_TAG master` causes non-reproducible builds | Pin to a specific release tag; bump intentionally |
| `GenerateExportFile` callback | Using `file_type` parameter to dispatch export type — but Logic 2 passes the same value for all export options in the current bug state | Read export intent from `mSettings->mExportFileType` rather than `file_type` parameter until the SDK bug is confirmed fixed |
| `AddExportOption` / `AddExportExtension` | Assuming these populate the Logic 2 export UI — they do not in current Logic 2 releases | Treat these as documentation only; implement the workaround via the settings-based dispatch |
| `FrameV2::AddString` for error messages | Strings are limited in length; very long error strings may be silently truncated | Keep error strings short; use codes rather than prose |
| `mResults->CommitResults()` in hot loop | Calling after every single slot adds overhead; no batching | Current code calls once per slot which is correct — do not move into the bit processing inner loop |
| `CheckIfThreadShouldExit()` | Not calling frequently enough causes slow plugin shutdown | Current code calls once per TDM frame which is correct for normal frame rates |

---

## Performance Traps

Patterns that work at small scale but fail as usage grows.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| `sprintf` in per-frame FrameV2 generation | CPU time in string formatting proportional to captured frames | Build error strings lazily or cache by flag bitmask | Noticeable at >100k frames (~2 seconds at 48kHz/256ch) |
| WAV header seek every 10ms | Slow export for long captures | Increase interval to 1000ms or update only at close | Noticeable on captures > 30 seconds |
| `new U64[mNumChannels]` in PCMWaveFileHandler constructor | Heap allocation for every export | Already heap-allocated once — acceptable; but `mSampleData` not RAII-managed, manual `delete[]` | Risk is delete on null if constructor fails after new; currently safe because file check precedes new |
| Linear frame iteration in `GenerateWAV()` | Export time scales with total frame count | Already linear — O(n) over frames is unavoidable | No performance trap here; already optimal |

---

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **WAV export:** Export completes without error, but verify channel alignment is preserved across frames that contain `SHORT_SLOT` errors — the current code does not guarantee this.
- [ ] **Error string formatting:** All four error conditions display correctly in the UI, but verify that simultaneous multi-error slots do not silently truncate the error string in the bubble text or FrameV2 output.
- [ ] **Cross-platform build:** All three platforms build and produce `.dll`/`.so`, but verify that the WAV header structs are the correct byte size on each platform by adding `static_assert` guards.
- [ ] **Settings round-trip:** Settings save and load correctly for all current fields, but verify that a project saved with the current version loads correctly after any future field addition (version number is missing).
- [ ] **Export file type selection:** The export type dropdown displays correctly when settings are loaded, but the constructor initializes the interface with `mEnableAdvancedAnalysis` instead of `mExportFileType` — verify initial default state is correct on fresh installation.
- [ ] **Simulation mode:** The simulation generator produces valid TDM frames, but `BITS_SHIFTED_RIGHT_1` mode inserts one leading zero bit (`BIT_LOW`) before the first data bit — verify this correctly simulates DSP Mode A timing as understood by the decoder.

---

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| sprintf buffer overflow | MEDIUM | Replace with snprintf at all call sites; remove warning suppression pragma; retest on all platforms |
| WAV export channel alignment drift | MEDIUM | Add zero-fill for SHORT_SLOT frames in GenerateWAV(); re-export affected captures |
| SDK master branch breaks build | LOW | `cmake --fresh` build, pin GIT_TAG to last known good commit hash from git log of the AnalyzerSDK repo |
| Settings format incompatibility | HIGH | Must decide: add migration logic or reset user settings; no recovery path without version identifier |
| WAV 4GB overflow | LOW | Re-export in segments; or implement pre-export size warning |
| Wrong export type on fresh install | LOW | User manually selects correct export type; one-line constructor fix |

---

## Pitfall-to-Phase Mapping

How audit/improvement phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| sprintf buffer overflow (Pitfall 1) | Audit — correctness pass | snprintf replaces all sprintf; pragma warning suppression removed; build with warnings-as-errors |
| Wrong variable in constructor (Pitfall 2) | Audit — code quality pass | Confirm constructor and UpdateInterfacesFromSettings use same variable for each interface |
| scalar_storage_order portability (Pitfall 3) | Audit — correctness pass | `static_assert` added for both WAV header struct sizes; confirmed builds pass on all three platforms |
| WAV 4GB overflow (Pitfall 4) | Audit — edge case pass | Pre-export size check added; documented in code |
| WAV header seek frequency (Pitfall 5) | Audit — performance pass | Update interval increased; benchmark export time on long synthetic captures |
| SHORT_SLOT channel alignment (Pitfall 6) | Audit — correctness pass | GenerateWAV() zero-fills missing slots; verify channel mapping across error frames with known test capture |
| GIT_TAG master instability (Pitfall 7) | Audit — build hygiene pass | ExternalAnalyzerSDK.cmake pins to specific tag; documented in CLAUDE.md |
| Settings version missing (Pitfall 8) | Audit — code quality pass | Version field added to archive; LoadSettings uses version-gated field reading |
| Misleading enum names (Pitfall 9) | Audit — code quality pass | Enum renamed; all call sites updated; documentation added |

---

## Sources

- Source code inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/` (direct, HIGH confidence)
- Codebase concerns audit: `.planning/codebase/CONCERNS.md` (direct, HIGH confidence)
- LLVM issue tracker: [Implement 'scalar_storage_order' attribute · Issue #34641 · llvm/llvm-project](https://github.com/llvm/llvm-project/issues/34641) — confirms Clang does not support this pragma (HIGH confidence)
- GCC documentation: [Structure-Layout Pragmas](https://gcc.gnu.org/onlinedocs/gcc/Structure-Layout-Pragmas.html) — confirms GCC-only support (HIGH confidence)
- WAV specification: [WAVE PCM soundfile format](http://soundfile.sapp.org/doc/WaveFormat/) — confirms 32-bit chunk size limit (HIGH confidence)
- RF64 spec: [EBU TECH 3306](https://tech.ebu.ch/docs/tech/tech3306v1_0.pdf) — confirms 4GB limit and RF64 as the remedy (HIGH confidence)
- Saleae discussion: [Export formats for low-level analyzers](https://discuss.saleae.com/t/export-formats-for-low-level-analyzers/1040) — confirms export type UI bug in Logic 2 (MEDIUM confidence)
- WAV 8-bit offset encoding: [The ABCs of PCM digital audio](http://blog.bjornroche.com/2013/05/the-abcs-of-pcm-uncompressed-digital.html) — confirms 8-bit WAV must be unsigned offset binary (HIGH confidence)
- sprintf safety: [snprintf vs sprintf](https://dev.to/ashok83/snprintf-vs-sprintf-a-deep-dive-into-buffer-overflows-prevention-59hg) — confirms snprintf as the correct replacement (HIGH confidence)

---
*Pitfalls research for: Saleae Logic 2 TDM Protocol Analyzer Plugin*
*Researched: 2026-02-23*
