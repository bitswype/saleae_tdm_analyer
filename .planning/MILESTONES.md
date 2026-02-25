# Milestones

## v1.0 TDM Analyzer Audit (Shipped: 2026-02-25)

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

