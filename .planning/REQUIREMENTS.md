# Requirements: TDM Analyzer Audit

**Defined:** 2026-02-23
**Core Value:** Correctly decode TDM audio data from logic analyzer captures with confidence that the results are accurate and the code is trustworthy.

## v1 Requirements

Requirements for this milestone. Each maps to roadmap phases.

### Correctness

- [ ] **CORR-01**: Fix sprintf buffer overflow risk in results formatting — use safe string formatting to prevent buffer overruns
- [ ] **CORR-02**: Fix settings initialization using wrong variable (mEnableAdvancedAnalysis instead of mExportFileType) — ensure export file type interface is initialized with the correct setting
- [ ] **CORR-03**: Fix WAV channel alignment drift after SHORT_SLOT error frames — ensure error frames don't corrupt channel mapping in exported WAV files
- [ ] **CORR-04**: Audit and fix ClearTabularText() compliance — ensure GenerateFrameTabularText() calls ClearTabularText() first to prevent Logic 2 crashes

### Build & Portability

- [ ] **BILD-01**: Pin AnalyzerSDK to commit hash 114a3b8 instead of master branch — ensure reproducible builds that don't break silently
- [ ] **BILD-02**: Add static_assert guards for WAV header struct sizes — compile-time verification replacing reliance on non-portable pragma scalar_storage_order

### Documentation

- [ ] **DOCS-01**: Clean up README build instructions — explain what each build step does, not just the commands, so the build process is understandable
- [ ] **DOCS-02**: Update WAV export documentation — reflect that the TXT/CSV export workaround is permanent (Saleae confirmed custom export types will not be implemented in Logic 2)

### Code Quality

- [ ] **QUAL-01**: Migrate std::auto_ptr to std::unique_ptr if present in codebase — remove deprecated C++ construct
- [ ] **QUAL-02**: Rename misleading enum values to match their actual purpose — improve code readability
- [ ] **QUAL-03**: Add WAV 4GB overflow pre-export warning — warn user when export data would exceed WAV format's 4GB RIFF chunk size limit

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Settings Enhancements

- **SETT-01**: Add sample rate sanity check in settings dialog — non-blocking warning when Logic analyzer sample rate is insufficient for configured TDM parameters
- **SETT-02**: Add named standard presets (I2S, Left-Justified, Right-Justified, DSP Mode A/B) — auto-fill settings from protocol name
- **SETT-03**: Settings dialog UX improvements — scope TBD

### SDK Modernization

- **SDKM-01**: Enrich FrameV2 data with additional structured fields
- **SDKM-02**: Add RF64 support for WAV exports exceeding 4GB

## Out of Scope

| Feature | Reason |
|---------|--------|
| New TDM format support | Current options cover all known I2S/TDM configurations |
| Custom export type API | Saleae confirmed this will not be implemented in Logic 2; workaround is permanent |
| Real-time streaming export | Architecturally impossible within Saleae SDK |
| Multi-data-channel support | SDK limitation — single data channel per analyzer instance |
| Python HLA migration | Major rewrite with no clear benefit; C++ SDK still supported |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| CORR-01 | — | Pending |
| CORR-02 | — | Pending |
| CORR-03 | — | Pending |
| CORR-04 | — | Pending |
| BILD-01 | — | Pending |
| BILD-02 | — | Pending |
| DOCS-01 | — | Pending |
| DOCS-02 | — | Pending |
| QUAL-01 | — | Pending |
| QUAL-02 | — | Pending |
| QUAL-03 | — | Pending |

**Coverage:**
- v1 requirements: 11 total
- Mapped to phases: 0
- Unmapped: 11 ⚠️

---
*Requirements defined: 2026-02-23*
*Last updated: 2026-02-23 after initial definition*
