# Roadmap: TDM Analyzer Audit

## Overview

This is an audit milestone for a feature-complete plugin. All TDM decoding capabilities are already shipped. The work is three phases of correctness, build hygiene, and code quality — in that order. Phase 1 removes known defects that silently corrupt output. Phase 2 locks down the build foundation. Phase 3 improves maintainability and documents the codebase for confident future extension.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: Correctness** - Fix all known defects that produce incorrect or undefined behavior
- [ ] **Phase 2: Build Hygiene** - Ensure reproducible builds and compile-time portability guards
- [ ] **Phase 3: Code Quality and Documentation** - Remove deprecated constructs, clarify names, and document the codebase

## Phase Details

### Phase 1: Correctness
**Goal**: All known correctness defects are eliminated; the analyzer produces accurate output under all conditions
**Depends on**: Nothing (first phase)
**Requirements**: CORR-01, CORR-02, CORR-03, CORR-04
**Success Criteria** (what must be TRUE):
  1. The codebase compiles without suppressed sprintf warnings; no `#pragma warning(disable: 4996)` blocks remain
  2. WAV export of a capture containing SHORT_SLOT frames writes correct channel data with no channel-position drift
  3. A settings object loaded from disk initializes the export file type control with the user's saved selection, not with a mismatched variable
  4. `GenerateFrameTabularText()` calls `ClearTabularText()` before any `AddTabularText()` call; Logic 2 does not crash on tabular data display
**Plans:** 1 plan
Plans:
- [ ] 01-01-PLAN.md — Fix all four correctness defects: sprintf safety, settings variable, WAV alignment, ClearTabularText compliance

### Phase 2: Build Hygiene
**Goal**: Builds are reproducible and WAV struct layout is verified at compile time across all supported compilers
**Depends on**: Phase 1
**Requirements**: BILD-01, BILD-02
**Success Criteria** (what must be TRUE):
  1. `ExternalAnalyzerSDK.cmake` specifies a pinned commit hash, not `master`; a fresh clone builds the same SDK regardless of when it runs
  2. The build fails at compile time with a descriptive error if `WavePCMHeader` or `WavePCMExtendedHeader` are not the expected byte sizes on any supported compiler
**Plans**: TBD

### Phase 3: Code Quality and Documentation
**Goal**: The codebase is free of deprecated constructs and misleading names, limits are communicated to the user, and documentation reflects the current permanent architecture
**Depends on**: Phase 2
**Requirements**: QUAL-01, QUAL-02, QUAL-03, DOCS-01, DOCS-02
**Success Criteria** (what must be TRUE):
  1. No `std::auto_ptr` usage exists in the codebase; all smart pointer usage is `std::unique_ptr` or equivalent modern C++11
  2. DSP mode enum values use names that match their protocol meaning; a developer reading the code understands the distinction without consulting external docs
  3. Attempting to export a WAV file that would exceed 4GB produces a user-visible warning before the export begins
  4. The README build section explains what each step does, not just the commands; a developer unfamiliar with CMake FetchContent can follow it
  5. WAV export documentation states the TXT/CSV workaround is the permanent architecture; no investigation note or "may be resolved" language remains
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Correctness | 0/1 | Not started | - |
| 2. Build Hygiene | 0/? | Not started | - |
| 3. Code Quality and Documentation | 0/? | Not started | - |
