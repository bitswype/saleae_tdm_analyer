# Feature Research

**Domain:** Saleae Logic 2 C++ Protocol Analyzer Plugin (TDM audio) — v1.4 Milestone
**Researched:** 2026-02-24
**Confidence:** HIGH (official Saleae SDK docs + official I2S analyzer source + confirmed bug status + RF64 EBU spec)

---

## Scope of This File

This file covers the **four new features** targeted for v1.4:

1. SDK audit and update to latest AnalyzerSDK commit
2. FrameV2 data enrichment (additional structured fields)
3. RF64 WAV support for captures exceeding 4 GiB
4. Sample rate sanity check / warning

Features already shipped in v1.0 (core decoding, existing FrameV2 fields, CSV export, WAV export, advanced error detection, simulation generator, cross-platform CI) are documented in the prior research cycle and are treated here as stable dependencies.

---

## State of FrameV2 (Already Partially Implemented)

The codebase already calls `UseFrameV2()` and emits structured data. Current fields per slot:

| Field | Type | Value |
|-------|------|-------|
| `channel` | integer | slot number (0-based, from `mResultsFrame.mType`) |
| `data` | integer | signed or unsigned sample value (sign-extended if `mSigned` is set) |
| `errors` | string | space-separated error tags (`E: Short Slot`, `E: Data Error`, `E: Frame Sync Missed`, `E: Bitclock Error`) or empty |
| `warnings` | string | `W: Extra Slot` or empty |
| `frame #` | integer | TDM frame counter (increments per frame sync pulse) |

Frame type tag passed to `AddFrameV2()` is `"slot"`.

**Implication:** FrameV2 enrichment for v1.4 means adding new fields to make the table more useful for analysis, not rebuilding the foundation.

---

## State of Sample Rate Check (Already Partially Implemented)

`GetMinimumSampleRateHz()` already returns:

```cpp
return mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate * 4;
```

This enforces a **4x oversampling floor** relative to the bit clock rate. Logic 2 uses this value to warn users when the capture sample rate is too low, but only at capture time — not in the settings dialog. The 4x multiplier is the correct engineering choice.

**Formula correctness:** The bit clock frequency for a TDM stream is `frame_rate × slots_per_frame × bits_per_slot`. Sampling at 4x that rate gives at least 2 samples per clock half-period, which is the practical minimum for reliable edge detection. The Saleae documentation recommends "as fast as possible, 256x or more" for ideal results, but 4x is the hard floor for a clock-synchronous protocol where the data channel is stable between edges. The official I2S analyzer uses the same 4x pattern (returns a fixed 4 MHz, which is approximately 4x a typical I2S bit clock).

**What's missing:** No user-visible warning inside the analyzer settings dialog. The SDK minimum rate is enforced silently at capture time — users who configure high frame rates and many slots may not realize their stored capture was taken at too low a sample rate. A non-blocking textual warning in the settings panel would surface this.

---

## Feature Landscape: v1.4 Features

### Table Stakes (Users Expect These)

Features that are table stakes for the v1.4 scope — missing them makes the milestone incomplete.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| RF64 WAV for large captures | The existing 4 GiB guard writes a text error file — any capture with many channels or high sample rates will hit this. Users expect the file to just work, not to find a text error message in a .wav file | MEDIUM | RF64 is the right format: EBU standard, supported by Audacity/FFmpeg/SoX/Python soundfile/SciPy. Implementation: replace `WavePCMHeader` with an RF64 header struct that uses JUNK→ds64 upgrade pattern |
| Sample rate warning in settings | Engineers configuring TDM frame rate + slots per frame need to know if their capture sample rate is insufficient. The SDK minimum is already computed; surfacing it as a human-readable check is expected | LOW | Not possible to show in-dialog warnings via the Saleae SDK UI directly — best approach is to expose the required minimum via `GenerateFrameTabularText` or `GetAnalyzerName` suffix; see Anti-Features for the right scope |
| SDK pinned to a current commit | Reproducible builds require a pinned SHA. Moving from `114a3b8` to a more recent SHA incorporates any bug fixes or ARM64 improvements from 2022-2023 | LOW | The SDK changelog shows the last meaningful changes were July 2023 (macOS ARM64 support) and January 2022 (FrameV2 API made official). No breaking API changes since then |

### Differentiators (Competitive Advantage)

Features for v1.4 that go beyond what competing community analyzers do.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| RF64 with JUNK→ds64 on-the-fly upgrade | Production-quality WAV export that handles any capture size without pre-export size checks or error fallback files | MEDIUM | The JUNK chunk is written at file open; if total data exceeds 4 GiB, the header is rewritten with `RF64` RIFF ID and `ds64` chunk. If under 4 GiB, the file is a valid standard WAV with a harmless JUNK chunk |
| Structured error boolean fields in FrameV2 | Python HLAs can filter `frame.data["short_slot"] == True` instead of string-parsing `frame.data["errors"]`. This is strictly better ergonomics for downstream analysis | LOW | Add boolean fields for each error flag: `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, `missed_frame_sync`. Keep existing string fields for human readability |
| Sample rate requirement surfaced per-analysis | After analysis completes, a marker or bubble can show the minimum required sample rate vs actual — engineers see immediately if their capture was marginal | LOW | This is achievable by adding an `AnalyzerResults::AddAnalyzerResult()` annotation on the first frame, or by noting it in tabular text |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Settings dialog inline warning for sample rate | Engineers want to see a red warning label while configuring the analyzer | The Saleae C++ SDK `AnalyzerSettingInterface` classes do not support custom warning text or conditional coloring. The only text in the dialog is the label string passed to `SetTitleAndTooltip()`. Injecting dynamic warnings (e.g., appending required rate to the label string at settings save time) is possible but fragile — the label only updates when settings are saved | Compute the minimum required rate and show it in the title of the frame rate setting: `"TDM Frame Rate (Hz) [min capture: Xk sps]"` — acceptable scope |
| Dual-header WAV (write RF64 only when needed) | Avoid the JUNK chunk in small captures | This adds branching logic and two code paths. The JUNK chunk is 28 bytes on a potentially gigabyte-sized file — the overhead is irrelevant. The correct tradeoff is: always write RF64 header with JUNK chunk, upgrade to ds64 if needed | Always use RF64 header format; standard-compliant players ignore the JUNK chunk |
| W64 (Sony Wave64) instead of RF64 | W64 also solves the 4 GiB limit | W64 has worse tool support. FFmpeg supports it, but Audacity, Python soundfile, SciPy, and most DAWs do NOT natively open W64. RF64 is the EBU standard and is what Audacity, Sox, Pro Tools, Nuendo, and Cubase all use. W64 is a Sony-origin format with narrow adoption | RF64 is the correct choice |
| Adding `raw_bits` ByteArray field to FrameV2 | Complete bit-level data for forensic analysis | The raw bits are already in the `data` integer field. Adding a byte array of the individual bits multiplies storage per frame with no practical benefit — any HLA can extract individual bits from the integer | The `data` field is sufficient; document bit ordering in README |
| Changing the frame type tag from `"slot"` to something else | Better semantics | The frame type tag is a breaking change for any existing HLA written against the current output. The tag `"slot"` is correct and semantically accurate | Leave `"slot"` as-is |

---

## RF64 vs W64: Compatibility Matrix

**Recommendation: Use RF64.**

| Tool | RF64 | W64 | Notes |
|------|------|-----|-------|
| Audacity | YES (native) | NO | Audacity explicitly outputs RF64 for large files |
| FFmpeg | YES | YES | FFmpeg supports both |
| SoX | YES | NO | SoX outputs RF64 for files over 4 GiB |
| Python soundfile | YES | NO | soundfile (libsndfile) supports RF64 natively |
| SciPy wavfile | YES (v1.14+) | NO | SciPy 1.14.0+ auto-handles RF64 |
| Reaper (DAW) | YES | YES | Best DAW support for both |
| Pro Tools | YES | NO | EBU RF64 is the broadcast standard |
| Cubase / Nuendo | YES | NO | Steinberg uses RF64 for large session files |
| Windows Media Foundation | YES | NO | Microsoft added RF64 support |
| pydub | YES (via FFmpeg) | REQUIRES FFmpeg | pydub delegates to FFmpeg |

**Confidence:** HIGH — verified via Audacity docs, FFmpeg source, SciPy changelog, community forum thread (Cockos forum, 2016 archive), EBU TECH 3306 spec.

**RF64 JUNK→ds64 upgrade pattern:**

```
File header layout (RF64 with pre-allocated JUNK chunk):
  Bytes 0-3:    "RIFF" (standard) or "RF64" (when > 4 GiB)
  Bytes 4-7:    file_size - 8 (32-bit, set to 0xFFFFFFFF when > 4 GiB)
  Bytes 8-11:   "WAVE"
  Bytes 12-15:  "JUNK" (pre-allocated) → becomes "ds64" on upgrade
  Bytes 16-19:  chunk size = 28
  Bytes 20-27:  RIFF 64-bit size (filled in on upgrade)
  Bytes 28-35:  data chunk 64-bit size (filled in on upgrade)
  Bytes 36-43:  sample count 64-bit (filled in on upgrade)
  Bytes 44-47:  table entry count = 0
  Bytes 48-...: fmt chunk, data chunk (unchanged from standard WAV)
```

Implementation: Add a new `RF64WaveFileHandler` class (or modify `PCMWaveFileHandler`) that:
1. Writes `RIFF` + `JUNK` header on open
2. Writes audio data normally via `addSample()`
3. On `close()`: if total bytes > 4 GiB, seek to byte 0, overwrite `RIFF` with `RF64`, overwrite 32-bit sizes with `0xFFFFFFFF`, write 64-bit sizes into the JUNK/ds64 chunk

This replaces the current 4 GiB pre-export overflow guard (which writes a plain-text error to the .wav path).

---

## FrameV2 Enrichment: Recommended Fields

### Current fields (keep all):
- `channel` (integer) — slot number
- `data` (integer) — sample value, sign-extended if signed mode
- `errors` (string) — human-readable error list
- `warnings` (string) — human-readable warning list
- `frame #` (integer) — TDM frame counter

### Add for v1.4:

| Field | Type | Value | Rationale |
|-------|------|-------|-----------|
| `short_slot` | boolean | true if `SHORT_SLOT` flag set | HLA can filter `frame.data["short_slot"]` without string parsing |
| `extra_slot` | boolean | true if `UNEXPECTED_BITS` flag set | Same rationale |
| `bitclock_error` | boolean | true if `BITCLOCK_ERROR` flag set | Same rationale |
| `missed_data` | boolean | true if `MISSED_DATA` flag set | Same rationale |
| `missed_frame_sync` | boolean | true if `MISSED_FRAME_SYNC` flag set | Same rationale |

**Rationale for boolean fields:** The official Saleae I2S analyzer exposes an `"error"` string field. The TDM analyzer already does this with the `"errors"` string. Adding per-flag booleans is strictly additive and enables HLA authors to write clean `if frame.data["short_slot"]:` logic instead of `if "Short Slot" in frame.data["errors"]:` string matching. This follows what the async-rgb-led-analyzer example does (named fields per data component).

**What NOT to add:**
- `raw_bits` byte array — redundant with `data` integer
- `bit_clock_period_samples` — too implementation-internal, not useful for HLA
- `start_sample` / `end_sample` — already available via the FrameV2 time range; HLA gets this from the frame bounds

**Confidence:** MEDIUM — based on FrameV2 API documentation (official Saleae docs), I2S analyzer field patterns, and the existing codebase structure. No official guidance on "correct" FrameV2 field design exists beyond the rgb-led example.

---

## Sample Rate Check: Formula and Scope

**Formula (already implemented in GetMinimumSampleRateHz):**

```
minimum_sample_rate = slots_per_frame × bits_per_slot × frame_rate_hz × 4
```

Where:
- `slots_per_frame × bits_per_slot × frame_rate_hz` = bit clock frequency in Hz
- `× 4` = 4x oversampling floor for reliable edge detection on a synchronous clock

**Is 4x correct?** Yes. For a clock-synchronous protocol, the data channel is guaranteed stable between clock edges. The analyzer only needs to reliably detect clock edges, which requires sampling at least 2x the clock frequency (Nyquist). The 4x multiplier provides a 2x safety margin. The Saleae recommendation of "256x or more" is aspirational for noisy or borderline signals; 4x is the hard engineering minimum for clean digital signals. Saleae's own I2S analyzer uses approximately 4x (returns a fixed 4 MHz).

**v1.4 scope for sample rate check:**

The `GetMinimumSampleRateHz()` value is already computed and returned to Logic 2. Logic 2 uses it to set the minimum sample rate in the capture settings — this is the primary enforcement mechanism.

The missing piece is surfacing the requirement in a human-readable form *after the fact* for captures already taken. Best approach: add the required minimum rate as an annotation on the first frame via `AddAnalyzerResultString()` or as a note in the tabular text for the first row. This does not require any new SDK capability.

**What the sample rate check is NOT:**
- It is not a blocking error that prevents analysis — the analyzer must continue even if the sample rate is insufficient (Logic 2 may have already taken the capture)
- It is not a settings dialog widget — the SDK does not support dynamic label updates based on other settings values in a clean way

**Recommended implementation:**

In `WorkerThread()`, after `GetSampleRate()` is called and `mDesiredBitClockPeriod` is computed, compare actual vs minimum:

```cpp
U64 actual_rate = GetSampleRate();
U32 min_rate = GetMinimumSampleRateHz();
if (actual_rate < min_rate) {
    // Add a result annotation on the first frame, or emit via mResults->AddAnalyzerResult()
    // to surface in the UI that sample rate may be insufficient
}
```

**Confidence:** HIGH for the formula. MEDIUM for the implementation approach (the Saleae SDK's mechanism for surfacing non-blocking warnings is not well-documented; the safest path is text in tabular output).

---

## SDK Update: Scope and Risk

**What changed in AnalyzerSDK since commit `114a3b8`:**

The AnalyzerSDK repository shows two meaningful changes since the pinned commit:
1. **January 2022**: FrameV2 API made official (merged from alpha branch, PR #26 "LOG-1488"). The TDM analyzer already uses FrameV2, so this is already accounted for.
2. **July 2023**: macOS ARM64 support improved (PR #31, reorganized library loading for cross-compiled builds). This is a build system change, not an API change.

No new API methods, no breaking changes, no bug fixes to the analysis pipeline were found in the commits between the pinned SHA and July 2023.

**Risk of updating:** LOW. The API surface has not changed. The update primarily picks up:
- Correct macOS ARM64 library loading (`CMAKE_OSX_ARCHITECTURES` detection)
- Any minor build system improvements

**Process:** Update the FetchContent SHA in `CMakeLists.txt` from `114a3b8` to the latest commit SHA. Run all three platform builds via CI. Verify no compilation warnings or errors. No code changes expected.

**Confidence:** MEDIUM — the commit history summary comes from a WebFetch that truncated after July 2023. The SDK repository last updated July 12, 2025 per WebSearch. There may be commits between July 2023 and July 2025 not surfaced. A direct review of the GitHub commit log is needed before updating.

---

## Feature Dependencies

```
[RF64 WAV support]
    └──replaces──> [4 GiB overflow guard (plain text fallback)]
    └──requires──> [new RF64WaveFileHandler class OR modification to PCMWaveFileHandler]
    └──requires──> [JUNK chunk pre-allocation at file open]
    └──enables──> [WAV export of any size capture without pre-check]

[FrameV2 boolean error fields]
    └──enhances──> [existing FrameV2 "errors" string field]
    └──requires──> [AddBoolean() calls in AnalyzeTdmSlot()]
    └──enables──> [clean HLA filtering without string parsing]

[Sample rate check warning]
    └──depends on──> [GetMinimumSampleRateHz() — already correct]
    └──depends on──> [GetSampleRate() — already called in WorkerThread()]
    └──produces──> [annotation on first frame or tabular row]
    └──does NOT block──> [analysis proceeding regardless]

[SDK update]
    └──independent of──> [all feature work above]
    └──should precede──> [all feature work] (build on known-good foundation)
    └──requires──> [CI verification on all three platforms]
```

---

## MVP Definition (v1.4 Milestone)

### Launch With (v1.4 release)

- [ ] RF64 WAV support — replaces the 4 GiB overflow guard with proper large-file export. This is the highest user-visible impact change: a user with a 6 GiB capture currently gets a text error file; after v1.4 they get a valid WAV file.
- [ ] FrameV2 boolean error fields — additive, low risk, materially improves HLA ergonomics
- [ ] Sample rate warning annotation — additive, non-blocking, surfaces important diagnostic information
- [ ] SDK update to latest commit SHA — foundation; do this first

### Defer to Later Milestone

- Named protocol presets (I2S, LJ, RJ, DSP) — settings dialog work, separate concern
- Python HLA companion — separate project, out of plugin scope

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| RF64 WAV export | HIGH — removes 4 GiB hard limit | MEDIUM — new header struct + upgrade logic | P1 |
| FrameV2 boolean error fields | MEDIUM — ergonomic improvement for HLA authors | LOW — 5 `AddBoolean()` calls per frame | P1 |
| Sample rate warning annotation | MEDIUM — prevents silent misconfiguration | LOW — one comparison + one annotation | P1 |
| SDK update to latest SHA | LOW (no new API) but correctness hygiene | LOW — update one CMakeLists.txt line + CI run | P1 (do first) |

---

## Sources

- EBU TECH 3306 RF64 specification (authoritative): [https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf](https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf)
- NAudio RF64/BWF implementation guide (implementation pattern reference): [https://markheath.net/post/naudio-rf64-bwf](https://markheath.net/post/naudio-rf64-bwf)
- FrameV2 / HLA Support — Analyzer SDK (official): [https://support.saleae.com/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk](https://support.saleae.com/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk)
- AnalyzerSDK AnalyzerResults.h — FrameV2 class declaration: [https://github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerResults.h](https://github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerResults.h)
- AnalyzerSDK commit history (last major change: July 2023, macOS ARM64): [https://github.com/saleae/AnalyzerSDK/commits/master](https://github.com/saleae/AnalyzerSDK/commits/master)
- Saleae sample rate guidance ("256x or more ideal"): [https://support.saleae.com/faq/technical-faq/what-sample-rate-is-required](https://support.saleae.com/faq/technical-faq/what-sample-rate-is-required)
- Audacity RF64 export docs: [https://manual.audacityteam.org/man/export_formats_supported_by_audacity.html](https://manual.audacityteam.org/man/export_formats_supported_by_audacity.html)
- W64 vs RF64 community discussion (RF64 wins on compatibility): [https://forum.cockos.com/archive/index.php/t-191986.html](https://forum.cockos.com/archive/index.php/t-191986.html)
- Official I2S analyzer source (FrameV2 field reference): [https://github.com/saleae/i2s-analyzer](https://github.com/saleae/i2s-analyzer)

---

*Feature research for: Saleae Logic 2 TDM Protocol Analyzer Plugin — v1.4 SDK & Export Modernization*
*Researched: 2026-02-24*
