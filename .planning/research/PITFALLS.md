# Pitfalls Research

**Domain:** Saleae Logic 2 C++ Protocol Analyzer Plugin — TDM/I2S Audio — v1.4 SDK & Export Modernization
**Researched:** 2026-02-24
**Confidence:** HIGH (direct source inspection + official SDK headers + RF64 spec + Audacity bug report + Saleae forum discussions)

---

## Critical Pitfalls

### Pitfall 1: RF64 ds64 Chunk Packed in the Wrong Position

**What goes wrong:**
RF64 inserts a `ds64` chunk between the RIFF/WAVE header and the `fmt` chunk. The correct byte layout is:

```
[0]  "RF64"        (4 bytes)   — replaces "RIFF"
[4]  0xFFFFFFFF   (U32)       — RIFF size sentinel (not real size)
[8]  "WAVE"        (4 bytes)
[12] "ds64"        (4 bytes)   — MUST come immediately after "WAVE"
[16] 28            (U32)       — ds64 chunk data size (always 28 bytes for PCM)
[20] riff_size_64  (U64)       — total file size minus 8
[28] data_size_64  (U64)       — true data chunk byte count
[36] sample_count  (U64)       — total sample frames written
[44] 0             (U32)       — table entry count (always 0 for PCM)
[48] "fmt "        (4 bytes)   — standard fmt chunk follows
...
[92] "data"        (4 bytes)
[96] 0xFFFFFFFF   (U32)       — data size sentinel (not real size)
[100] <sample data>
```

If the `ds64` chunk is placed after `fmt` or `data`, compliant readers will not find it because they expect it immediately after `WAVE`. If `#pragma pack(1)` is not active for the ds64 struct, the struct will be padded and the written bytes will be wrong.

**Why it happens:**
Developers copying from WAV code assume the chunk ordering is flexible (RIFF allows arbitrary chunk ordering within the WAVE container). It is not — RF64 readers scan for `ds64` at a fixed expected offset and will skip it if out of position. Also, the JUNK-to-ds64 migration approach adds code paths that can place the ds64 chunk in the wrong location if the byte-level offset tracking is off by even one byte.

**How to avoid:**
Write the RF64 header as a single packed struct or write each field explicitly in the correct sequence using the existing `writeLittleEndianData()` pattern already in `PCMWaveFileHandler`. Add a `static_assert` on the RF64 header struct size (expected: 100 bytes for the full header through the `data` chunk ID and sentinel, before sample data). Do NOT use the JUNK-to-ds64 approach; instead, always write RF64 headers directly when RF64 mode is selected (the TDM analyzer knows its data size before writing — it has `num_frames` and `mFrameSizeBytes` computed in `GenerateWAV()`).

**Warning signs:**
- Audacity opens the file but reports 0 samples or an incorrect duration
- VLC or ffprobe reports "invalid data found when processing input" or "Could not find codec parameters"
- File plays at wrong speed (reader used fmt sample rate but calculated wrong sample count)

**Phase to address:** RF64 implementation phase — design the struct layout and verify with a hex dump against the EBU Tech 3306 spec before writing any sample data.

---

### Pitfall 2: Both RIFF Size and Data Chunk Size Sentinel Values Must Be 0xFFFFFFFF — Not Zero

**What goes wrong:**
RF64 requires two 32-bit size fields to be set to the sentinel value `0xFFFFFFFF` (not 0, not the real size) to signal to readers that the true size is in the `ds64` chunk:
1. The RIFF chunk size at offset 4 (always `0xFFFFFFFF`)
2. The data chunk size at offset 96 (assuming standard PCM header layout, always `0xFFFFFFFF`)

If either field is written as 0, or as the truncated 32-bit real size, readers will either reject the file as corrupt or compute an incorrect size, stopping playback early.

**Why it happens:**
Code that computes chunk sizes and then clamps to UINT32_MAX is a common but wrong "fix." Clamping produces a valid 32-bit number; the sentinel `0xFFFFFFFF` is specifically -1 cast to U32, which most implementations treat as "use the 64-bit value in ds64 instead." An implementation that writes `min(real_size, 0xFFFFFFFF)` for a file exactly at 4GB will write the sentinel by accident, which works — but for files slightly over 4GB it will write the wrong truncated value.

**How to avoid:**
For RF64 files: always write `0xFFFFFFFF` for both the RIFF size field and the data chunk size field in the primary header. Write the true 64-bit sizes only in the `ds64` chunk. Use named constants:
```cpp
constexpr U32 RF64_SIZE_SENTINEL = 0xFFFFFFFFu;
```
Cross-reference: EBU Tech 3306 Section 3.1 — "The value of the 'size' field of the 'RIFF' chunk shall be set to -1."

**Warning signs:**
- `ffprobe` reports "Invalid data found" or "data size 4294967295" (that is 0xFFFFFFFF, which means it read the sentinel from the primary header rather than the ds64)
- File plays but duration shown is "4 hours 46 minutes" regardless of actual content (reader computed duration from `0xFFFFFFFF / sample_rate`)

**Phase to address:** RF64 implementation phase — enforce sentinel constants at the time the RF64 header struct is defined, not in the write path.

---

### Pitfall 3: RF64 ds64 Sizes Must Be Updated at File Close — Forgetting the Seek-Back

**What goes wrong:**
The `ds64` chunk contains three 64-bit fields (RIFF size, data size, sample count) that are not known until after all samples have been written. The ds64 chunk is written at the beginning of the file (bytes 12–47), but the correct values can only be determined at the end. This requires:
1. Writing placeholder zeros for these fields at open time
2. After writing all sample data, seeking back to byte 20 and overwriting with the final values

If the seek-back is omitted or seeks to the wrong offset, the ds64 chunk will contain zeros, and the reader will compute 0-byte data and 0 samples — the file will appear empty.

The existing `PCMWaveFileHandler::updateFileSize()` uses U32 arithmetic (`mTotalFrames * mFrameSizeBytes`) for the size computation. This cannot be directly reused for RF64 — the same multiplication must use U64.

**Why it happens:**
The existing WAV code's `updateFileSize()` pattern is familiar to copy from. Developers copy the seek positions from the existing `RIFF_CKSIZE_POS = 4` and `DATA_CKSIZE_POS = 40` constants and forget that RF64 shifts these offsets (data chunk ID is at byte 92 in RF64, not byte 36). Also, the existing update uses U32 types throughout; silently using U32 for the ds64 fields defeats the entire purpose of RF64.

**How to avoid:**
Define explicit constants for the RF64 ds64 field offsets:
```cpp
// RF64 header layout (PCM, standard format, no extra chunks)
constexpr U64 RF64_DS64_RIFF_SIZE_OFFSET   = 20;  // within ds64 chunk data
constexpr U64 RF64_DS64_DATA_SIZE_OFFSET   = 28;
constexpr U64 RF64_DS64_SAMPLE_COUNT_OFFSET = 36;
```
In `close()`, seek to each offset and write the U64 value using the existing `writeLittleEndianData()` with `num_bytes = 8`. Verify with: after close, reopen the file and read back these fields — confirm they match the expected sizes.

**Warning signs:**
- File has correct size on disk but plays as silence for zero duration
- `ffprobe` reports `size=0` in the ds64 chunk
- The existing `updateFileSize()` implementation uses `U32` for any size variable

**Phase to address:** RF64 implementation phase — the `close()` method is the critical path; test with a known-size synthetic write before attempting a real capture.

---

### Pitfall 4: FrameV2 `AddFrameV2` Requires `UseFrameV2()` in the Analyzer Constructor — Already Done, but Must Not Be Removed

**What goes wrong:**
The current `TdmAnalyzer` constructor calls `UseFrameV2()` at line 13 of `TdmAnalyzer.cpp`. This registers the analyzer as a FrameV2 producer. If this call is removed during an SDK update (e.g., by overwriting the constructor when migrating to a newer SDK template), FrameV2 data submitted via `AddFrameV2()` will be silently ignored — no error, no crash, just an empty data table.

Separately: the SDK documentation notes that analyzers using FrameV2 cannot be loaded in Logic 2 versions older than 2.3.43. This is already the case; do not regress it by removing `UseFrameV2()`.

**Why it happens:**
When updating the SDK, developers sometimes regenerate boilerplate from a template or copy from a reference analyzer that predates `UseFrameV2()`. The call is in the constructor body, not a header, so it is easy to miss in a diff.

**How to avoid:**
In any SDK update commit, explicitly verify `UseFrameV2()` is still present in `TdmAnalyzer::TdmAnalyzer()`. Add a comment above it:
```cpp
// Required: registers this analyzer as a FrameV2 producer.
// Without this, AddFrameV2() data is silently dropped and the data table will be empty.
// Requires Logic 2.3.43+. Do not remove without understanding the implications.
UseFrameV2();
```

**Warning signs:**
- After SDK update, the data table in Logic 2 shows no rows despite analyzer running successfully
- `GenerateFrameTabularText()` is never called (Logic 2 only calls it for FrameV2 analyzers)

**Phase to address:** SDK update phase — add this comment as part of the SDK audit commit.

---

### Pitfall 5: FrameV2 Field Keys With Spaces or Special Characters Are Silently Accepted but May Break HLA Access

**What goes wrong:**
The current FrameV2 implementation uses `"frame #"` as a field key (with a space and special character). FrameV2 stores arbitrary string keys, and the SDK does not validate or reject keys at write time — so `frame_v2.AddInteger("frame #", mFrameNum)` compiles and runs. However, Python HLA scripts access FrameV2 fields via `frame.data["frame #"]` dictionary syntax. The space and hash make this key work as a Python string literal, but it breaks if HLA authors use attribute-style access or if any future Saleae tooling normalizes field names (replacing spaces with underscores).

More critically: field keys must be consistent across all calls to `AddFrameV2()` for the same frame type. If any code path adds a key for some frames but not others, the data table column for that key will show empty cells intermittently, which is confusing but not a crash.

**Why it happens:**
The existing `"frame #"` key was written without checking HLA access patterns. The hash character is legal in a C string but unusual as a dictionary key. Similarly, adding conditional fields (only adding "errors" when there are errors) creates inconsistent column presence across frames.

**How to avoid:**
Rename `"frame #"` to `"frame_num"` — no spaces, no special characters, consistent with Saleae's own analyzer examples which use all-lowercase underscore-separated keys (`"identifier"`, `"num_data_bytes"`, `"remote_frame"`). Always add all FrameV2 fields for every frame, even if the value is empty string or 0:
```cpp
frame_v2.AddString("errors", error_str);    // always add, even if ""
frame_v2.AddString("warnings", warning_str); // always add, even if ""
frame_v2.AddInteger("frame_num", mFrameNum);
```

**Warning signs:**
- HLA script raises `KeyError` when accessing `frame.data["frame #"]`
- Some rows in the data table show empty cells in the "frame #" column when errors occur (because error frames used a different code path)

**Phase to address:** FrameV2 enrichment phase — fix key naming when implementing any new FrameV2 fields.

---

### Pitfall 6: Settings SimpleArchive Append-Only Rule — New Fields Added in Wrong Order Break Old Projects

**What goes wrong:**
`LoadSettings()` reads `SimpleArchive` fields in strict sequential order. The current end-of-archive fields are:
```
... mFrameSyncInverted → mExportFileType → mEnableAdvancedAnalysis
```
Adding a new field (e.g., `mShowSampleRateWarning`) requires appending it after `mEnableAdvancedAnalysis` in both `LoadSettings()` and `SaveSettings()`. If it is inserted in the middle — before `mEnableAdvancedAnalysis` — then all projects saved by an older build will load the `bool mEnableAdvancedAnalysis` value into `mShowSampleRateWarning`, and `mEnableAdvancedAnalysis` will be read from end-of-stream (defaulting to whatever the member variable was initialized to).

This is a silent data corruption — no error is thrown, the plugin loads successfully, but settings are wrong.

**Why it happens:**
Settings fields are often grouped logically in code (all "analysis" settings together, all "export" settings together). A developer adding a sample-rate warning toggle might insert it near the other analysis settings for clarity. The SimpleArchive read order must follow the exact write order, not the logical grouping.

**How to avoid:**
The current codebase does not have a version number in the archive (confirmed: `LoadSettings` checks magic string only, then reads fields without a version gate). Before adding any new field, add a `U32 version` field immediately after the magic string check:
```cpp
// SaveSettings: write version 2 with new field
text_archive << "SaleaeTdmAnalyzer";
text_archive << U32(2); // settings format version

// LoadSettings: read version, gate new fields
U32 settings_version = 1; // default: assume old format
text_archive >> settings_version;
// ... existing field reads ...
if (settings_version >= 2) {
    // read new field
}
```
Document the version→fields mapping with a comment block at the top of `LoadSettings()`.

**Warning signs:**
- `SaveSettings()` and `LoadSettings()` field order diverges even by one position
- A new field is added to SaveSettings in alphabetical or logical order rather than at the end
- No `U32 version` field in the archive after the magic string

**Phase to address:** Sample rate validation phase — any new settings field triggers this risk. Add the version field before adding `mShowSampleRateWarning` or any other new setting.

---

### Pitfall 7: Sample Rate Warning in `SetSettingsFromInterfaces()` Cannot Be Non-Blocking — Only `SetErrorText()` Exists

**What goes wrong:**
The SDK's `AnalyzerSettings` base class provides `SetErrorText(const char* error_text)` which, when called in `SetSettingsFromInterfaces()`, causes Logic 2 to display a red error message. The method requires `SetSettingsFromInterfaces()` to return `false` to indicate rejection. There is no `SetWarningText()` method and no way to return `true` (accept settings) while also displaying a visible warning message to the user in the settings panel.

If the sample rate check calls `SetErrorText()` and returns `false`, the user cannot start analysis — the settings panel stays open. This is wrong for a "non-blocking warning" about potentially-insufficient sample rate: the user should be allowed to proceed with their analysis even if the sample rate is low.

**Why it happens:**
Developers see `SetErrorText()` and assume it is the right tool for any feedback. It is not — it is specifically a validation rejection mechanism.

**How to avoid:**
Do not use `SetErrorText()` for the sample rate warning. The correct approach is to surface the warning through the analysis itself:
1. Call `SetErrorText()` and return `false` **only** for hard configuration errors (impossible bit clock period, zero frame rate, etc.)
2. For the sample rate sanity check: compute `GetMinimumSampleRateHz()` in `SetSettingsFromInterfaces()` and if `mSampleRate < minimum`, add a warning marker or annotation during `WorkerThread()` when the first frame is processed — or surface it as an info-level string in the first FrameV2 result
3. Alternatively, display a short informational string in the title of one of the settings interfaces using `SetToolTipText()` on `mTdmFrameRateInterface` if the SDK supports it

The cleanest approach: add a `mSampleRateWarning` boolean computed during `WorkerThread()` initialization (after `mSampleRate = GetSampleRate()`) and emit a FrameV2 with type `"warning"` as the first result if the sample rate is insufficient. This is visible in the data table without blocking analysis.

**Warning signs:**
- Sample rate check calls `SetErrorText()` and returns `false` — user is blocked from running the analyzer
- Sample rate check is in `WorkerThread()` but produces no visible user-facing output (just silently produces bad frames)

**Phase to address:** Sample rate validation phase — decide the UX approach before implementation, not during.

---

### Pitfall 8: Nyquist Threshold for TDM Is Bit Clock Frequency, Not Frame Rate — Off-by-Frame-Size Factor

**What goes wrong:**
The minimum sample rate for a logic analyzer to correctly capture a TDM bit clock is determined by Nyquist for the bit clock frequency, not the audio frame rate. The bit clock frequency is:

```
bit_clock_hz = mTdmFrameRate * mSlotsPerFrame * mBitsPerSlot
```

The Saleae minimum sample rate requirement (`GetMinimumSampleRateHz()`) already computes this correctly. However, Nyquist requires the sample rate to be at least twice the bit clock frequency — in practice, for clean digital capture, 4× is the minimum useful oversampling ratio (Saleae's own documentation recommends 4× the data rate). If the check uses `2×` instead of `4×`, it will pass configurations that produce unreliable capture at the logic analyzer's limits.

For example: 48 kHz audio, 2 slots, 32 bits per slot → bit clock = 48000 × 2 × 32 = 3.072 MHz → minimum useful capture rate = 12.288 MHz. Most Logic 8 and Logic 16 devices support 12 MHz but not exactly 12.288 MHz — the check will issue a false positive warning even when the user selects the closest available rate.

**Why it happens:**
The check is written as `sample_rate < 2 * bit_clock_hz` (Nyquist) rather than `sample_rate < 4 * bit_clock_hz` (practical minimum for digital capture). The Saleae `GetMinimumSampleRateHz()` already returns 4× the bit clock frequency, so the check should compare against `GetMinimumSampleRateHz()` rather than recomputing its own threshold.

**How to avoid:**
Use the existing `GetMinimumSampleRateHz()` return value as the threshold:
```cpp
U32 min_rate = GetMinimumSampleRateHz();
U32 current_rate = GetSampleRate();  // only available in WorkerThread(), not SetSettingsFromInterfaces()
if (current_rate < min_rate) {
    // warn user
}
```
Note: `GetSampleRate()` is only callable from `WorkerThread()`, not from `SetSettingsFromInterfaces()`. The check must go in `WorkerThread()`. `mSettings->mTdmFrameRate` is the user-configured value but `GetSampleRate()` is the actual hardware sample rate — they are independent.

**Warning signs:**
- Warning fires for every capture where the user has selected a reasonable but not-quite-4× sample rate
- Warning uses `mTdmFrameRate` as the threshold without accounting for `mSlotsPerFrame * mBitsPerSlot`
- Warning check is in `SetSettingsFromInterfaces()` where `GetSampleRate()` is not available

**Phase to address:** Sample rate validation phase — derive the threshold from `GetMinimumSampleRateHz()` rather than recomputing independently.

---

### Pitfall 9: SDK Update May Expose New Header-Only Methods That Are Compile-Errors Under C++11

**What goes wrong:**
The AnalyzerSDK has been stable at commit `114a3b8` since July 2023. Updating to a newer commit (if one exists) may introduce C++ standard library features or syntax that requires C++14 or C++17. The project's `CMakeLists.txt` locks to C++11 via `set(CMAKE_CXX_STANDARD 11)`. Header-only SDK changes that use `auto` deduction, `std::make_unique`, `constexpr if`, or structured bindings will fail to compile under C++11, but only if those headers are included (which they will be — the SDK headers are the core include).

Additionally: the SDK ships precompiled binaries (`.so`/`.dll`) with fixed ABI. If a new SDK commit changes the ABI (virtual function table layout, class member layout), the plugin will load and immediately crash or produce undefined behavior — no compile error will warn you.

**Why it happens:**
The SDK is a Saleae-internal project with no formal SemVer. Commit messages do not always announce ABI changes. The precompiled binary is built by Saleae with their toolchain settings; there is no guarantee it is C++11 compatible at the header level.

**How to avoid:**
Before updating the pinned SDK commit, inspect the diff between `114a3b8` and the new commit for:
1. New include directives (especially `<optional>`, `<variant>`, `<any>` which are C++17)
2. `auto` return type deduction in headers
3. Changed virtual function signatures (ABI break)
4. New or removed methods in `Analyzer2`, `AnalyzerResults`, `AnalyzerSettings` base classes

If an ABI break is found: update the GIT_TAG only after verifying on all three platforms in CI. Do not change `CMAKE_CXX_STANDARD` to a newer value without a full rebuild and test cycle on all platforms.

Based on the current GitHub commit history of `saleae/AnalyzerSDK`, `114a3b8` is the HEAD of master as of July 2023 — there are no later commits visible. The SDK audit should verify this (no update needed) before treating the SDK pin as requiring a change.

**Warning signs:**
- Build fails with `error: 'auto' not allowed in function prototype` or `error: 'std::make_unique' was not declared` after updating GIT_TAG
- Logic 2 crashes immediately after loading the plugin (no error dialog — hard crash indicates ABI mismatch)
- `ldd` / `otool -L` shows the plugin links against a different SDK binary than the one in the build output directory

**Phase to address:** SDK audit phase — diff first, update second, test on all three platforms before merging.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| JUNK-to-ds64 RF64 migration approach | Allows graceful fallback for files under 4GB | Complex code paths, difficult to test, known source of offset bugs | Never in this codebase — always write RF64 headers directly when RF64 is selected |
| Periodic `updateFileSize()` every 10ms carried into RF64 | Copy of existing WAV pattern | RF64 requires U64 seek back to `ds64`; copying U32 pattern silently truncates | Not acceptable — RF64 update must use U64 arithmetic throughout |
| `"frame #"` field key with space and hash | Existed since FrameV2 introduction | Breaks HLA scripts using attribute access; fragile if Saleae normalizes keys | Fix it now before HLA ecosystem builds on the broken key |
| Using `SetErrorText()` for non-blocking sample rate advisory | Only warning mechanism visible in the settings UI | Blocks user from running analysis; the warning becomes a hard error | Never — use FrameV2 or WorkerThread marker for non-blocking feedback |
| Computing sample rate threshold independently of `GetMinimumSampleRateHz()` | Looks cleaner in SetSettingsFromInterfaces() | Creates divergence between two places computing the same threshold | Never — delegate to `GetMinimumSampleRateHz()` |

---

## Integration Gotchas

Common mistakes when connecting to the Saleae SDK and RF64 spec.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| RF64 header write | Using JUNK chunk approach ("write WAV, convert to RF64 if > 4GB") | Write RF64 header from the start when RF64 mode is selected; the data size is computed before export in `GenerateWAV()` |
| FrameV2 field keys | Using `"frame #"` (space + hash) or other non-identifier characters | Use underscore-separated lowercase: `"frame_num"`, consistent with Saleae reference analyzers |
| FrameV2 conditional fields | Only adding `"errors"` key when errors exist | Always emit every field for every frame; missing keys cause sparse columns in the data table |
| `GetSampleRate()` in settings | Calling from `SetSettingsFromInterfaces()` — not available there | Only callable from `WorkerThread()` context; store in `mSampleRate` during `WorkerThread()` init |
| `SetErrorText()` for advisory | Using it for non-blocking warnings about sample rate | Returns false → blocks analysis; advisory goes in first FrameV2 result or WorkerThread output |
| RF64 `mTotalFrames` counter | Using `U32 mTotalFrames` inherited from PCMWaveFileHandler | RF64 sample count in ds64 is U64; a U32 wraps at ~4 billion frames |
| SimpleArchive append-only | Inserting new settings field in logical position in the file | Always append new fields at the end of the archive sequence |

---

## Performance Traps

Patterns that work at small scale but fail as usage grows.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| RF64 ds64 update on every flush (inherited from WAV pattern) | Excessive seeks for long exports; each seek writes 24 bytes at offset 20-43 | Update ds64 only in `close()` for RF64 — the cancel-recovery rationale for periodic updates still applies, but increase interval to 1000ms | Noticeable on captures > 60 seconds at high channel count |
| `mTotalFrames` as U32 in RF64 handler | Frame counter wraps at 4,294,967,295; ds64 sample count shows wrong value | Define a separate `RF64WaveFileHandler` with U64 frame/sample counters throughout | Breaks at ~4 billion TDM frames (approximately 25 hours at 48kHz/256ch) |
| FrameV2 string formatting per slot with fixed buffer | CPU proportional to frame count; unchanged from current code | Current `snprintf` into `char[80]` is acceptable; do not replace with heap-allocating `std::string` per frame | Acceptable at all expected capture sizes |
| Writing RF64 sample data as individual `writeLittleEndianData()` byte-by-byte calls | Very slow for large exports due to per-byte `ofstream::write()` | Already the current pattern; acceptable unless profiling shows I/O as hot path; could batch with a write buffer | Acceptable at current scale; revisit if export of >1 GiB is slow |

---

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **RF64 header:** File opens in Audacity without error — verify `ffprobe -v error -show_entries stream=nb_samples,duration` matches expected sample count and duration, not just that the file opens.
- [ ] **RF64 ds64 fields:** File size on disk is correct — verify the `ds64` chunk at byte offset 20 contains the correct 64-bit values by examining the file with a hex editor or `xxd | head`.
- [ ] **RF64 data chunk sentinel:** Data chunk size field reads 0xFFFFFFFF — verify at byte offset 96 (standard PCM RF64 header layout) the four bytes are `FF FF FF FF`.
- [ ] **FrameV2 field presence:** Every row in the Logic 2 data table has all columns populated — open a capture with error frames and verify "errors" and "warnings" columns are not blank for any row.
- [ ] **Settings round-trip with new field:** After adding `mShowSampleRateWarning` (or any new field) with version gate, verify that: (a) a project saved by the old build loads correctly (new field gets default value), and (b) a project saved by the new build loads correctly in the new build.
- [ ] **Sample rate warning fires at right threshold:** Warning fires for a 3.072 MHz bit clock with a 6 MHz sample rate (below 4× minimum), but does NOT fire when sample rate is 12 MHz (at 4× minimum) — check both cases.
- [ ] **SDK update does not break existing builds:** After changing GIT_TAG, confirm all three platform builds pass in CI before merging.
- [ ] **RF64 `static_assert` on header struct size:** The RF64 header struct must have a `static_assert` for its size (100 bytes for PCM RF64 through the data chunk ID and sentinel) on all three platforms.

---

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| RF64 ds64 at wrong byte offset | MEDIUM | Identify correct offset with `xxd` on a known-good RF64 file; fix struct layout; re-export |
| RIFF/data sentinel not 0xFFFFFFFF | LOW | Add `RF64_SIZE_SENTINEL` constant; verify in test harness with known-size write |
| ds64 not updated at close | LOW | Verify `close()` calls `seekp()` to ds64 offsets; add hex-dump assertion in test |
| `UseFrameV2()` removed during SDK update | LOW | Add it back to constructor; no data migration needed |
| `"frame #"` key breaks HLA | MEDIUM | Rename key to `"frame_num"`; any existing HLA scripts must update their key lookup |
| Settings field insertion breaks old projects | HIGH | Add version field; implement migration; bump version; document in changelog |
| Sample rate check blocks analysis | LOW | Move from `SetSettingsFromInterfaces()` to `WorkerThread()` with FrameV2 output |
| SDK ABI break on update | HIGH | Revert GIT_TAG to `114a3b8`; wait for SDK release with documented compatibility |
| U32 frame counter overflow in RF64 | MEDIUM | Change RF64 handler to use U64 for frame/sample counts throughout |

---

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| RF64 ds64 chunk wrong position (Pitfall 1) | RF64 implementation | `ffprobe` shows correct duration and sample count; `xxd` confirms ds64 at byte 12 |
| RF64 sentinel values not 0xFFFFFFFF (Pitfall 2) | RF64 implementation | Hex dump confirms `FF FF FF FF` at RIFF size (offset 4) and data size (offset 96) |
| RF64 ds64 not updated at close (Pitfall 3) | RF64 implementation | Write 1000 known samples, close, reopen, verify ds64 data_size_64 == expected |
| UseFrameV2() removed during SDK update (Pitfall 4) | SDK audit | Data table shows rows after SDK update; add comment in constructor |
| FrameV2 field keys with spaces (Pitfall 5) | FrameV2 enrichment | Data table columns present for all rows including error frames; no KeyError in HLA |
| Settings archive order broken by new field (Pitfall 6) | Any phase adding new settings field | Old-build .sal file loads in new build with correct values; new-build .sal round-trips cleanly |
| Sample rate warning blocks analysis (Pitfall 7) | Sample rate validation | Warning appears in data table as FrameV2 row, not as settings rejection; user can run analysis |
| Wrong Nyquist threshold (Pitfall 8) | Sample rate validation | Warning fires at < 4× bit clock frequency; does not fire at exactly 4× |
| SDK update C++11 breakage or ABI mismatch (Pitfall 9) | SDK audit | Full CI build on all three platforms passes after changing GIT_TAG |

---

## Sources

- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/` — all pitfalls grounded in actual code at specific lines (HIGH confidence)
- EBU Tech 3306 v1.0 RF64 specification — confirms ds64 chunk structure, sentinel value requirements, chunk ordering (HIGH confidence via search result excerpts)
- PlayPcmWin RF64 documentation: https://sourceforge.net/p/playpcmwin/wiki/RF64%20WAVE/ — confirms sentinel values and chunk layout (MEDIUM confidence)
- NAudio RF64 implementation article: https://markheath.net/post/naudio-rf64-bwf — confirms JUNK-to-ds64 approach complexity and testing difficulty (MEDIUM confidence)
- libsndfile RF64 spec problems blog (Erik de Castro Lopo): http://www.mega-nerd.com/erikd/Blog/CodeHacking/libsndfile/rf64_specs.html — confirms ds64 RIFFSize update pitfall and on-the-fly conversion complexity (HIGH confidence)
- Audacity RF64 bug report: https://forum.audacityteam.org/t/rf64-with-4gb-size-is-mistaken-as-wav/54380 — confirms Audacity had a bug where RF64 was mistaken for WAV; demonstrates fragility of tool-side RF64 support (MEDIUM confidence)
- Saleae AnalyzerSDK AnalyzerResults.h on GitHub: https://github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerResults.h — confirms FrameV2 class definition, method signatures, #ifdef LOGIC2 guard (HIGH confidence)
- Saleae AnalyzerSDK AnalyzerSettings.h on GitHub: https://github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerSettings.h — confirms SetErrorText() is the only user-visible message mechanism (HIGH confidence)
- Saleae CAN analyzer reference implementation: https://github.com/saleae/can-analyzer/blob/master/src/CanAnalyzer.cpp — confirms field naming conventions (lowercase underscore, no special characters) (HIGH confidence)
- Saleae FrameV2 community discussion: https://discuss.saleae.com/t/framev2-api/1320 — confirms non-overlapping frame requirement, single track output constraint (MEDIUM confidence)
- AnalyzerSDK commit history fetch: confirms 114a3b8 is master HEAD as of July 2023; no newer commits visible (MEDIUM confidence — fetched via WebFetch)

---
*Pitfalls research for: Saleae Logic 2 TDM Protocol Analyzer Plugin — v1.4 SDK & Export Modernization*
*Researched: 2026-02-24*
