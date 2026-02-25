# Requirements: TDM Analyzer v1.4

**Defined:** 2026-02-25
**Core Value:** Correctly decode TDM audio data from logic analyzer captures with confidence that the results are accurate and the code is trustworthy.

## v1.4 Requirements

Requirements for this milestone. Each maps to roadmap phases.

### SDK Modernization

- [ ] **SDKM-01**: Audit AnalyzerSDK pin — verify 114a3b8 is latest HEAD, document finding in commit message
- [ ] **SDKM-02**: Remove unused `mResultsFrameV2` member variable from TdmAnalyzer.h — dead state that creates confusion
- [ ] **SDKM-03**: Rename FrameV2 key `"frame #"` to `"frame_number"` — space and hash in key breaks HLA Python attribute-style access

### FrameV2 Enrichment

- [ ] **FRM2-01**: Add boolean `short_slot` field to FrameV2 output for each decoded slot — enables HLA filtering without string parsing
- [ ] **FRM2-02**: Add boolean `extra_slot` field to FrameV2 output for each decoded slot
- [ ] **FRM2-03**: Add boolean `bitclock_error` field to FrameV2 output for each decoded slot
- [ ] **FRM2-04**: Add boolean `missed_data` field to FrameV2 output for each decoded slot
- [ ] **FRM2-05**: Add boolean `missed_frame_sync` field to FrameV2 output for each decoded slot
- [ ] **FRM2-06**: Add 1-based `slot` field alongside existing 0-based `channel` field — human-readable in data table

### RF64 WAV Export

- [ ] **RF64-01**: Create WaveRF64Header packed struct (80 bytes: RF64 root 12 + ds64 36 + fmt 20 + data header 8 + padding 4) with static_assert size guard
- [ ] **RF64-02**: Create RF64WaveFileHandler class with U64 frame/sample counters and ds64 seek-back-at-close to write true sizes
- [ ] **RF64-03**: Modify GenerateWAV to use RF64WaveFileHandler when estimated data exceeds 4 GiB, standard PCMWaveFileHandler otherwise
- [ ] **RF64-04**: Remove existing 4 GiB text-error guard from GenerateWAV — replaced by conditional RF64 path

### Sample Rate Validation

- [ ] **SRAT-01**: Add non-blocking sample rate warning annotation on first analysis frame when capture sample rate is below 4× bit clock rate — uses FrameV2 or tabular text, not SetErrorText
- [ ] **SRAT-02**: Add hard rejection in SetSettingsFromInterfaces for physically impossible TDM parameter combinations requiring sample rate >500 MSPS — uses SetErrorText to block analysis

## Future Requirements

Deferred to later milestones. Tracked but not in current roadmap.

### Settings Enhancements

- **SETT-01**: Named standard presets (I2S, Left-Justified, Right-Justified, DSP Mode A/B) — auto-fill settings from protocol name
- **SETT-02**: Settings dialog UX improvements — scope TBD
- **SETT-03**: Settings format version field — needed if future milestone adds persistent new settings fields

### Advanced Export

- **AEXP-01**: Python HLA companion for post-processing decoded TDM data — separate project

## Out of Scope

| Feature | Reason |
|---------|--------|
| New TDM format support | Current options cover all known I2S/TDM configurations |
| Custom export type API | Saleae confirmed this will not be implemented in Logic 2; TXT/CSV workaround is permanent |
| Real-time streaming export | Architecturally impossible within Saleae SDK |
| Multi-data-channel support | SDK limitation — single data channel per analyzer instance |
| Python HLA migration | Major rewrite with no clear benefit; C++ SDK still supported |
| W64 format (Sony) | Narrow tool adoption (FFmpeg only); RF64 is the EBU standard with broad support |
| Always-RF64 mode | Conditional RF64 chosen for maximum compatibility with older tools |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| SDKM-01 | — | Pending |
| SDKM-02 | — | Pending |
| SDKM-03 | — | Pending |
| FRM2-01 | — | Pending |
| FRM2-02 | — | Pending |
| FRM2-03 | — | Pending |
| FRM2-04 | — | Pending |
| FRM2-05 | — | Pending |
| FRM2-06 | — | Pending |
| RF64-01 | — | Pending |
| RF64-02 | — | Pending |
| RF64-03 | — | Pending |
| RF64-04 | — | Pending |
| SRAT-01 | — | Pending |
| SRAT-02 | — | Pending |

**Coverage:**
- v1.4 requirements: 15 total
- Mapped to phases: 0
- Unmapped: 15 ⚠️

---
*Requirements defined: 2026-02-25*
*Last updated: 2026-02-25 after initial definition*
