# Roadmap: TDM Analyzer

## Milestones

- [x] **v1.3 Audit** - Phases 1-3 (shipped 2026-02-25)
- [x] **v1.4 SDK & Export Modernization** - Phases 4-7 (shipped 2026-02-26)

## Phases

<details>
<summary>v1.3 Audit (Phases 1-3) - SHIPPED 2026-02-25</summary>

See: `.planning/milestones/v1.0-ROADMAP.md` for full phase details.

Phase 1: Correctness Audit — 4 bug fixes in src/
Phase 2: Build Hygiene — SDK pin, static_assert guards
Phase 3: Code Quality and Documentation — enum rename, README rewrite, WAV overflow guard

</details>

<details>
<summary>v1.4 SDK & Export Modernization (Phases 4-7) - SHIPPED 2026-02-26</summary>

See: `.planning/milestones/v1.4-ROADMAP.md` for full phase details.

Phase 4: SDK Audit & Housekeeping — SDK pin verified, dead code removed, FrameV2 key renamed
Phase 5: FrameV2 Enrichment — ten-field schema with boolean error fields and severity enum
Phase 6: Sample Rate Validation — settings guards and non-blocking advisory annotation
Phase 7: RF64 WAV Export — RF64 handler and conditional dispatch for >4 GiB exports

</details>

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Correctness | v1.3 | 1/1 | Complete | 2026-02-25 |
| 2. Build Hygiene | v1.3 | 1/1 | Complete | 2026-02-25 |
| 3. Code Quality & Docs | v1.3 | 2/2 | Complete | 2026-02-25 |
| 4. SDK Audit & Housekeeping | v1.4 | 1/1 | Complete | 2026-02-25 |
| 5. FrameV2 Enrichment | v1.4 | 1/1 | Complete | 2026-02-26 |
| 6. Sample Rate Validation | v1.4 | 1/1 | Complete | 2026-02-26 |
| 7. RF64 WAV Export | v1.4 | 2/2 | Complete | 2026-02-26 |
