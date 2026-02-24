# Feature Research

**Domain:** Saleae Logic 2 C++ Protocol Analyzer Plugin (TDM audio)
**Researched:** 2026-02-23
**Confidence:** HIGH (official Saleae SDK docs + official I2S analyzer source + confirmed bug status)

---

## Custom Export Type Bug: Status Assessment

### Verdict: NOT FIXED — Workaround Still Required

**Confidence:** HIGH

The bug where `AddExportOption` / `AddExportExtension` in C++ analyzer plugins produce no visible menu items in Logic 2's export UI has **not been fixed** as of Logic 2.4.40 (December 2025 — the latest version found in the changelog).

Evidence:
- Logic 2.4.22 still broken: community reports confirm `AddExportOption` / `AddExportExtension` do not work (verified in search results, 2024)
- The feature request at `ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/` remains **Open** with no planned implementation
- Saleae's official stance (as of October 2021, the most recent statement): they intend to **pare back** the C++ SDK's export functionality, not restore it. The intended path is Python HLA extensions
- The official Saleae I2S analyzer source still uses only one export type (txt/csv) — no custom binary types
- The Logic 2 changelog (versions 2.3.x through 2.4.40) contains no entry restoring custom export types

**Implication for TDM analyzer:** The current WAV export workaround (hijacking the TXT/CSV export callback, selecting format via a settings dropdown) is the correct and only viable approach. This is not a temporary workaround — it is the permanent solution for as long as Logic 2's C++ SDK export API remains broken.

---

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist. Missing these = product feels incomplete.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Bubble text on signal waveform | Core SDK capability; every analyzer has it; zero use without it | LOW | TDM has this. Multiple levels of detail (short/medium/long) are expected |
| Correct decoding of all configured variants | The only reason to use the plugin at all | HIGH | TDM has I2S, LJ, RJ, DSP modes via settings; this is the core value |
| Configurable channel assignments | Users may probe any physical pins | LOW | TDM has clock/frame/data channel selection |
| Signed/unsigned integer display | Audio samples are signed two's complement; raw hex is useless for debugging audio | LOW | TDM has this; official I2S analyzer has it too |
| CSV export with timing and values | Engineers want to post-process data in Excel/Python | LOW | TDM has this with Time[s], Channel, Value, Flags columns |
| Error markers on waveform | Visual indication of protocol violations at the sample level | LOW | TDM has arrow markers; errors flagged as DISPLAY_AS_ERROR_FLAG |
| Simulation data generator | Without it, the plugin is untestable without hardware | MEDIUM | TDM has it (sine wave + counter + static generators) |
| FrameV2 / Data Table support | Required for Logic 2's data table panel and HLA chaining since Logic 2.3.43 | LOW | TDM has this (`UseFrameV2()` in constructor, `AddFrameV2()` per slot) |
| GenerateFrameTabularText() implementation | Drives the legacy tabular text view; expected to be non-empty | LOW | TDM has this with channel, value, error, warning strings |
| Settings persist across sessions | Logic 2 serializes settings automatically — but analyzer must expose all settings via interfaces | LOW | TDM has this via `LoadSettings`/`SaveSettings` and interface objects |
| Cross-platform builds | Saleae runs on Windows, macOS, Linux — a plugin only on one platform is half-broken | MEDIUM | TDM has CI/CD for all three via GitHub Actions |

### Differentiators (Competitive Advantage)

Features that set the product apart. Not required, but valued.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| WAV export of decoded audio | Lets engineers listen to what the bus is carrying — unique to audio-domain analyzers | HIGH | TDM has this via workaround (settings dropdown selects WAV vs CSV); handles 1-256 channels, 8-64 bit depth |
| TDM multi-slot support (1-256 slots/frame) | I2S is always 2-channel; TDM encoders can have 32+ channels; no other community analyzer covers this | HIGH | TDM's primary differentiator; official I2S analyzer is hard-coded to 1-4 subframes |
| Advanced error detection (bitclock jitter, missed data, missed frame sync) | Helps identify marginal hardware quickly; most analyzers stop at "too few bits" | MEDIUM | TDM has this behind `mEnableAdvancedAnalysis` flag; optional to avoid false positives |
| Configurable slot size separate from data width | Real TDM hardware often sends 32-bit slots with 24-bit audio; most tools don't handle this cleanly | LOW | TDM has `mBitsPerSlot` vs `mDataBitsPerSlot` as separate settings |
| Multiple simulation data patterns | Sine wave tests audio correctness; counter tests slot boundaries; static tests known values | MEDIUM | TDM has all three in the simulation generator |
| Extra slot / short slot detection | Flags frame sync misalignment issues visible only at protocol level, not at hardware level | LOW | TDM has both with dedicated flag bits |
| FrameV2 with structured error fields | Downstream HLA or Python analysis can filter on error type without string parsing | LOW | TDM adds "errors", "warnings", "frame #", "channel", "data" as named FrameV2 fields |

### Anti-Features (Commonly Requested, Often Problematic)

Features that seem good but create problems.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Custom export type via AddExportOption | Cleaner UX — WAV and CSV as distinct menu items | Logic 2 C++ SDK does not render these menu items at all; implementing it produces invisible options; Saleae has stated intent to deprecate this capability further | Use settings dropdown (current approach): ExportFileType enum selects WAV vs CSV at export time |
| Real-time streaming WAV (pipe to player) | Audio engineers want instant playback | Requires OS-level IPC, not possible within the analyzer plugin sandbox; export is file-based only | Export WAV file, open in any audio player |
| Named protocol presets (I2S, TDM-32, etc.) | Reduces configuration steps for common formats | Requires UI changes Saleae doesn't expose; also couples settings to preset names that don't have universal definitions | Document the settings for each common format in the README; user saves Logic 2 project files as presets |
| Sample rate sanity check / warning | Prevents silent under-sampling errors | Logic 2 doesn't provide a mechanism to show warnings in the settings UI without blocking the analyzer from starting; would require invasive workaround | `GetMinimumSampleRateHz()` already enforces minimum via SDK; add a README note about required sample rates |
| Multiple data channels per analyzer instance | Some TDM hardware has multi-wire data buses | SDK architecture assigns one data channel per analyzer; users can add multiple analyzer instances | Document multi-instance approach in README |
| Python HLA companion for post-processing | Extends analysis to application-layer | Separate project; adds maintenance burden; users who need HLA can write their own against TDM's FrameV2 output | The FrameV2 fields ("channel", "data", "errors", "warnings", "frame #") are named cleanly for HLA consumption |

---

## Feature Dependencies

```
[FrameV2 support]
    └──enables──> [Data Table display in Logic 2 UI]
    └──enables──> [HLA chaining (Python post-processing)]
    └──requires──> [UseFrameV2() in Analyzer constructor]

[WAV export]
    └──requires──> [Correct slot data in Frame.mData1]
    └──requires──> [ExportFileType setting in TdmAnalyzerSettings]
    └──depends on──> [WAV export workaround (not SDK custom export types)]
    └──constrains──> [mTdmFrameRate setting must be provided by user — SDK cannot derive it from capture]

[Advanced error detection]
    └──requires──> [mEnableAdvancedAnalysis setting]
    └──produces──> [MISSED_DATA, MISSED_FRAME_SYNC, BITCLOCK_ERROR flags in Frame]
    └──enhances──> [FrameV2 "errors" field]

[Multi-slot TDM (>2 slots)]
    └──requires──> [mSlotsPerFrame setting]
    └──enables──> [WAV export with >2 channels]
    └──complicates──> [Bubble text (slot numbers, not channel names like "L"/"R")]

[Signed integer display]
    └──requires──> [mSigned setting]
    └──affects──> [GenerateBubbleText, GenerateCSV, GenerateFrameTabularText, FrameV2 "data" field]
```

### Dependency Notes

- **FrameV2 requires UseFrameV2():** If `UseFrameV2()` is not called in the constructor, Logic 2 v2.3.43+ ignores `AddFrameV2()` calls silently. The data table and HLA chaining will not work.
- **WAV export depends on user-provided sample rate:** Unlike CSV, WAV requires a known `mTdmFrameRate` (audio sample rate in Hz) embedded in the file header. The analyzer cannot derive this from the logic capture alone — it's a protocol parameter the user must configure correctly. This is a known limitation.
- **Advanced analysis conflicts with noisy hardware:** Enabling `mEnableAdvancedAnalysis` on signals with clock jitter will produce false-positive `BITCLOCK_ERROR` flags. The feature is correctly guarded behind a settings bool.

---

## MVP Definition (Audit Milestone Context)

This is not a greenfield MVP — the plugin is already feature-complete for its stated scope. The audit milestone is about raising confidence in correctness, not adding features.

### Already Shipped (v1 — current state)

- [x] Core TDM decoding (I2S, LJ, RJ, DSP variants) — existing
- [x] FrameV2 with named fields — existing
- [x] CSV export with timing, flags, signed/unsigned values — existing
- [x] WAV export via TXT/CSV workaround — existing, confirmed permanent
- [x] Advanced error detection (bitclock, missed data, missed frame sync) — existing
- [x] Multi-slot support (1-256 slots) — existing
- [x] Simulation data generator — existing
- [x] Cross-platform CI/CD — existing

### Potentially Add After Audit (v1.x — out of current scope)

- [ ] Sample rate sanity check warning — deferred to future milestone
- [ ] Named preset configurations (I2S, RJ, LJ, DSP) — deferred to future milestone

### Confirmed Out of Scope (v2+)

- [ ] Custom export type via SDK (AddExportOption) — blocked by Logic 2 bug, no fix in sight
- [ ] Real-time audio streaming — architecturally impossible in plugin sandbox
- [ ] Multi-data-channel support within one analyzer instance — requires SDK architectural changes

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Correct core decoding | HIGH | Already done | P1 (done) |
| FrameV2 data table support | HIGH | Already done | P1 (done) |
| WAV export via workaround | HIGH | Already done | P1 (done) |
| Signed integer display | HIGH | Already done | P1 (done) |
| CSV export | HIGH | Already done | P1 (done) |
| Advanced error detection | MEDIUM | Already done | P2 (done) |
| Simulation data generator | MEDIUM | Already done | P2 (done) |
| Sample rate sanity check | MEDIUM | LOW | P2 (future milestone) |
| Named presets | LOW | MEDIUM | P3 (future milestone) |
| Custom export type (AddExportOption) | LOW | HIGH + blocked by SDK bug | P3 / never |

---

## Comparison with Official Saleae I2S Analyzer

The official Saleae I2S analyzer is the closest reference point.

| Feature | Official I2S Analyzer | TDM Analyzer | Notes |
|---------|----------------------|--------------|-------|
| FrameV2 | Yes (`UseFrameV2()`) | Yes (`UseFrameV2()`) | Both correct |
| Channel count | Fixed: 1-4 subframes via `PcmFrameType` enum | Configurable: 1-256 via `mSlotsPerFrame` | TDM is strictly more capable |
| Signed integer display | Yes | Yes | Both correct |
| Export types | CSV only | CSV + WAV (via workaround) | TDM adds unique value |
| Error detection | Too-few-bits, doesn't-divide-evenly | Short slot, extra slot, bitclock error, missed data, missed frame sync | TDM is strictly more capable |
| FrameV2 fields | "channel", "data" | "channel", "data", "errors", "warnings", "frame #" | TDM richer for HLA use |
| GenerateFrameTabularText | Yes | Yes | Both correct |
| Simulation generator | Yes | Yes (sine + counter + static) | TDM more patterns |
| Settings count | 9 settings | 12 settings | TDM covers more protocol variants |
| Slot size vs data width separation | No (bits_per_word is both) | Yes (`mBitsPerSlot` vs `mDataBitsPerSlot`) | TDM handles real hardware better |

**Assessment:** The TDM analyzer is at least as capable as the official I2S reference implementation in every dimension, and substantially more capable in the dimensions that matter for TDM (multi-slot, WAV export, fine-grained error detection, slot size separation).

---

## Sources

- Saleae Changelog (verified up to v2.4.40, Dec 2025): [https://ideas.saleae.com/f/changelog/](https://ideas.saleae.com/f/changelog/)
- Custom Export Options Feature Request (status: Open, no planned implementation): [https://ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/](https://ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/)
- Export formats for low-level analyzers discussion (confirmed not implemented in Logic 2): [https://discuss.saleae.com/t/export-formats-for-low-level-analyzers/1040](https://discuss.saleae.com/t/export-formats-for-low-level-analyzers/1040)
- Missing V1 SDK features thread (Saleae intent: pare back C++ SDK, push to Python HLAs): [https://discuss.saleae.com/t/missing-v1-analyzer-sdk-plugin-features-and-ideas/684](https://discuss.saleae.com/t/missing-v1-analyzer-sdk-plugin-features-and-ideas/684)
- FrameV2 / HLA Support - Analyzer SDK (official): [https://support.saleae.com/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk](https://support.saleae.com/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk)
- Official Saleae I2S Analyzer source (reference implementation): [https://github.com/saleae/i2s-analyzer](https://github.com/saleae/i2s-analyzer)
- Saleae Analyzer API documentation: [https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md](https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md)

---
*Feature research for: Saleae Logic 2 TDM Protocol Analyzer Plugin*
*Researched: 2026-02-23*
