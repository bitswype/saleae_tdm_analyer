---
phase: 02-build-hygiene
plan: 01
subsystem: infra
tags: [cmake, fetchcontent, static_assert, wav, build-reproducibility, portability]

# Dependency graph
requires: []
provides:
  - Pinned AnalyzerSDK FetchContent to full 40-char commit SHA (reproducible builds)
  - Compile-time static_assert guards for WavePCMHeader (44 bytes) and WavePCMExtendedHeader (80 bytes)
affects: [all future phases that modify WAV structs, any contributors setting up fresh builds]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Use full 40-char SHA in GIT_TAG for reproducible FetchContent builds
    - static_assert after #pragma pack(pop) region to catch packing errors on all compilers

key-files:
  created: []
  modified:
    - cmake/ExternalAnalyzerSDK.cmake
    - src/TdmAnalyzerResults.h

key-decisions:
  - "Pin AnalyzerSDK to 114a3b8306e6a5008453546eda003db15b002027 — last known-good July 2023 commit"
  - "Place static_assert outside pragma pack/scalar_storage_order region — catches Clang/MSVC packing errors that GCC-only scalar_storage_order would miss"
  - "GIT_SHALLOW True retained — compatible with commit hash pinning in CMake 3.11+"

patterns-established:
  - "FetchContent pinning: always use full 40-char SHA, never branch/tag names, with explanatory comment block"
  - "Struct size validation: static_assert placed after closing pragma pack(pop) and scalar_storage_order default"

requirements-completed: [BILD-01, BILD-02]

# Metrics
duration: 2min
completed: 2026-02-25
---

# Phase 2 Plan 01: Build Hygiene — SDK Pin and WAV Struct Guards Summary

**AnalyzerSDK pinned to immutable commit SHA with explanatory comments, and compile-time static_assert guards added for both WAV header structs (44 and 80 bytes) outside pragma pack regions**

## Performance

- **Duration:** 2 min
- **Started:** 2026-02-25T06:29:16Z
- **Completed:** 2026-02-25T06:31:20Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- Eliminated non-reproducible SDK builds: GIT_TAG master replaced with full 40-char SHA 114a3b8306e6a5008453546eda003db15b002027
- Added static_assert(sizeof(WavePCMHeader) == 44) after WavePCMHeader's closing pragma region
- Added static_assert(sizeof(WavePCMExtendedHeader) == 80) after WavePCMExtendedHeader's closing pragma region
- Both guards include descriptive messages identifying the spec and what to check on failure

## Task Commits

Each task was committed atomically:

1. **Task 1: Pin AnalyzerSDK FetchContent to commit hash (BILD-01)** - `9c1f39e` (chore)
2. **Task 2: Add static_assert guards for WAV header struct sizes (BILD-02)** - `a35165a` (feat)

## Files Created/Modified
- `cmake/ExternalAnalyzerSDK.cmake` - GIT_TAG changed from mutable `master` to full 40-char SHA with explanatory comment block
- `src/TdmAnalyzerResults.h` - Two static_assert guards added after each WAV header struct's closing pragma pack/scalar_storage_order region

## Decisions Made
- Pinned to 114a3b8306e6a5008453546eda003db15b002027 as the last known-good SDK commit from July 2023 (specified in plan)
- GIT_SHALLOW True was retained — this flag is compatible with commit hash pinning in CMake 3.11+ and avoids fetching full repo history
- static_assert placement is after `#pragma pack(pop)` AND `#pragma scalar_storage_order default` — fully outside both pragma regions. This ensures the assertions evaluate the final packed struct layout
- The existing `#pragma scalar_storage_order` directives were preserved (not removed) as they document intent and provide enforcement on GCC

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None - both modifications were straightforward text edits with clear placement instructions in the plan.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Build reproducibility is now guaranteed: any fresh clone fetches the identical SDK version
- Struct size enforcement is active on all compilers: GCC (scalar_storage_order + static_assert), Clang and MSVC (static_assert only)
- BILD-01 and BILD-02 requirements are fulfilled
- Ready to proceed to remaining Phase 2 plans (if any)

## Self-Check: PASSED

- cmake/ExternalAnalyzerSDK.cmake: FOUND
- src/TdmAnalyzerResults.h: FOUND
- .planning/phases/02-build-hygiene/02-01-SUMMARY.md: FOUND
- commit 9c1f39e (Task 1): FOUND
- commit a35165a (Task 2): FOUND

---
*Phase: 02-build-hygiene*
*Completed: 2026-02-25*
