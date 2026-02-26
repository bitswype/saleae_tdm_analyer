# Roadmap: TDM Analyzer v1.4 SDK & Export Modernization

## Milestones

- [x] **v1.3 Audit** - Phases 1-3 (shipped 2026-02-25)
- [ ] **v1.4 SDK & Export Modernization** - Phases 4-7 (in progress)

## Phases

<details>
<summary>v1.3 Audit (Phases 1-3) - SHIPPED 2026-02-25</summary>

See: `.planning/milestones/v1.0-ROADMAP.md` for full phase details.

Phase 1: Correctness Audit — 4 bug fixes in src/
Phase 2: Build Hygiene — SDK pin, static_assert guards
Phase 3: Code Quality and Documentation — enum rename, README rewrite, WAV overflow guard

</details>

### v1.4 SDK & Export Modernization (In Progress)

**Milestone Goal:** Update the AnalyzerSDK to latest, enrich decoded data with FrameV2 structured fields, add RF64 support for large WAV exports, and warn users about insufficient sample rates.

## Phase Details

### Phase 4: SDK Audit and Housekeeping
**Goal**: The SDK pin is verified as current HEAD, dead code is removed, and FrameV2 field keys are fixed so HLA scripts can access frame fields without workarounds
**Depends on**: Phase 3 (v1.3 complete)
**Requirements**: SDKM-01, SDKM-02, SDKM-03
**Success Criteria** (what must be TRUE):
  1. A commit message documents that 114a3b8 is confirmed as AnalyzerSDK HEAD with the date of verification
  2. TdmAnalyzer.h no longer contains a `mResultsFrameV2` member variable
  3. FrameV2 output uses the key `"frame_number"` (not `"frame #"`) — HLA Python can access it as `frame.data["frame_number"]` without error
**Plans**: 1 plan

Plans:
- [x] 04-01-PLAN.md — SDK audit, dead member removal, FrameV2 key rename, and v2.0.0 documentation

### Phase 5: FrameV2 Enrichment
**Goal**: Every decoded slot in the Logic 2 data table carries structured boolean error fields and a severity enum, with the channel field renamed to slot, enabling HLA scripts to filter error frames without string parsing
**Depends on**: Phase 4
**Requirements**: FRM2-01, FRM2-02, FRM2-03, FRM2-04, FRM2-05, FRM2-06, FRM2-07
**Success Criteria** (what must be TRUE):
  1. The Logic 2 data table shows `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, and `missed_frame_sync` boolean columns for every decoded slot row — including error-free rows (which show false)
  2. The data table shows a `slot` column with 0-based values replacing the former `channel` column — no `channel` column exists
  3. The data table shows a `severity` column with values `error`, `warning`, or `ok` — replacing the former `errors` and `warnings` string columns
  4. An HLA script can filter for error frames using `frame.data["short_slot"] == True` without AttributeError or KeyError
  5. All boolean and severity fields are emitted on every slot frame regardless of whether the frame contains errors
**Plans**: 1 plan

Plans:
- [x] 05-01-PLAN.md — Rewrite FrameV2 block with nine-field schema and document breaking changes

### Phase 6: Sample Rate Validation
**Goal**: Users with inadequate capture sample rates receive an advisory warning during analysis, and users who configure physically impossible TDM parameters are blocked before analysis runs
**Depends on**: Phase 5
**Requirements**: SRAT-01, SRAT-02
**Success Criteria** (what must be TRUE):
  1. When the Logic analyzer sample rate is below 4x the configured bit clock rate, the first frame in the analysis output contains a warning annotation visible in the Logic 2 data table — and analysis completes normally (not blocked)
  2. When configured TDM parameters require a sample rate above 500 MSPS, the settings dialog shows an error message and prevents analysis from running
  3. The sample rate advisory does not prevent the user from running analysis — it is informational only
**Plans**: 1 plan

Plans:
- [ ] 06-01-PLAN.md — Settings validation (zero-param guards + 500 MHz hard block) and non-blocking advisory annotation with per-slot low_sample_rate boolean

### Phase 7: RF64 WAV Export
**Goal**: WAV exports larger than 4 GiB produce a valid RF64 file that opens in Audacity, FFmpeg, and other standard tools — rather than a plain-text error file at the .wav path
**Depends on**: Phase 6
**Requirements**: RF64-01, RF64-02, RF64-03, RF64-04
**Success Criteria** (what must be TRUE):
  1. Exporting a capture that would exceed 4 GiB produces a file with RIFF ID `RF64`, a valid ds64 chunk at byte offset 12, and sentinel value `0xFFFFFFFF` at the RIFF chunk size field — verified by `ffprobe` or hex dump
  2. Exporting a capture under 4 GiB produces a standard PCM WAV file (no RF64 overhead) — existing tooling compatibility is preserved
  3. The 4 GiB text-error guard is gone — no code path writes a plain-text message to the .wav file path
  4. The exported RF64 file opens and plays correctly in Audacity with the correct channel count, sample rate, and duration
**Plans**: TBD

Plans:
- [ ] 07-01: WaveRF64Header struct and RF64WaveFileHandler class
- [ ] 07-02: GenerateWAV conditional dispatch and 4 GiB guard removal

## Progress

**Execution Order:**
Phases execute in numeric order: 4 → 5 → 6 → 7

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Correctness | v1.3 | 1/1 | Complete | 2026-02-25 |
| 2. Build Hygiene | v1.3 | 1/1 | Complete | 2026-02-25 |
| 3. Code Quality & Docs | v1.3 | 2/2 | Complete | 2026-02-25 |
| 4. SDK Audit & Housekeeping | v1.4 | 1/1 | Complete | 2026-02-25 |
| 5. FrameV2 Enrichment | v1.4 | Complete    | 2026-02-26 | 2026-02-26 |
| 6. Sample Rate Validation | 1/1 | Complete   | 2026-02-26 | - |
| 7. RF64 WAV Export | v1.4 | 0/2 | Not started | - |
