# Milestones

## v1.3 TDM Analyzer Audit (Shipped: 2026-02-25)

**Phases completed:** 3 phases, 4 plans, 8 tasks
**Files modified:** 8 (source + cmake + README)
**Git range:** aeaa093..57b7830

**Key accomplishments:**
- Fixed 4 correctness bugs: sprintf buffer overflows, settings init mismatch, WAV channel alignment drift, ClearTabularText compliance
- Pinned AnalyzerSDK to full commit SHA for reproducible builds
- Added compile-time static_assert guards for WAV header struct sizes (44 and 80 bytes)
- Renamed TdmBitAlignment enum values to protocol-semantic DSP_MODE_A/DSP_MODE_B
- Added WAV 4 GiB pre-export overflow guard preventing silent file corruption
- Rewrote README build instructions with FetchContent explanation; WAV export docs reframed as permanent Saleae architecture

---


## v1.4 SDK & Export Modernization (Shipped: 2026-02-26)

**Phases completed:** 4 phases, 5 plans, 12 tasks
**Files modified:** 8 (432 insertions, 73 deletions)
**Lines of code:** 2,425 C++ (up from 2,103)

**Key accomplishments:**
- SDK audited (114a3b8 confirmed HEAD), dead mResultsFrameV2 member removed, FrameV2 key fixed for HLA compatibility (v2.0.0 tag)
- Ten-field FrameV2 schema: slot, data, frame_number, severity, 5 boolean error fields, low_sample_rate — all emitted unconditionally
- Settings validation: zero-param guards and 500 MHz bit clock hard block prevent impossible configurations
- Non-blocking sample rate advisory emitted as FrameV2 row 0 when capture rate < 4x bit clock
- RF64 WAV export for captures exceeding 4 GiB per EBU TECH 3306 — 80-byte header, U64 counters, ds64 seekback
- Conditional RF64/PCM dispatch in GenerateWAV replaces text-error abort guard

---

