# Roadmap: TDM Analyzer

## Milestones

- [x] **v1.3 Audit** - Phases 1-3 (shipped 2026-02-25)
- [x] **v1.4 SDK & Export Modernization** - Phases 4-7 (shipped 2026-02-26)
- [ ] **v1.5 Python HLA WAV Companion** - Phases 8-10 (in progress)

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

## v1.5 Python HLA WAV Companion

### Phase 8: HLA Scaffold & Settings
Set up the `hla/` directory as a valid Logic 2 extension. Create `extension.json`, the main Python class skeleton, and settings definitions (slots, output_path, bit_depth). Verify the HLA loads in Logic 2 without errors.

**Deliverables:** `hla/extension.json`, `hla/TdmWavExport.py` (skeleton), settings wired to Logic 2 UI

**Plans:** 1/1 plans complete

Plans:
- [ ] 08-01-PLAN.md — Create hla/extension.json and TdmWavExport.py scaffold with settings

### Phase 9: Core WAV Writing
Implement slot filtering from the settings, WAV file creation and writing using Python's `wave` module, and periodic header refresh so partial captures are recoverable.

**Deliverables:** Full `decode()` implementation, slot parser, WAV writer with frame-based header refresh

**Plans:** 1/1 plans complete

Plans:
- [ ] 09-01-PLAN.md — Implement parse_slot_spec, _as_signed helpers and full WAV writing decode()

### Phase 10: Error Handling & Documentation
Harden error paths (invalid slots, missing output path, LLA error frames), update README with HLA installation and usage instructions.

**Deliverables:** Error frame emission, graceful error frame handling, README `hla/` section

**Plans:** 1/1 plans complete

Plans:
- [ ] 10-01-PLAN.md — Harden __init__ error paths (deferred error pattern) and add README HLA section

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
| 8. HLA Scaffold & Settings | v1.5 | 1/1 | Complete | 2026-03-03 |
| 9. Core WAV Writing | v1.5 | 1/1 | Complete | 2026-03-03 |
| 10. Error Handling & Docs | 1/1 | Complete    | 2026-03-03 | — |
