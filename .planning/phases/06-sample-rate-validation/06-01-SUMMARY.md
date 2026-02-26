---
phase: 06-sample-rate-validation
plan: 01
subsystem: analyzer
tags: [cpp, framev2, settings-validation, sample-rate, advisory]

# Dependency graph
requires:
  - phase: 05-framev2-enrichment
    provides: nine-field FrameV2 schema with severity/boolean fields that this plan extends to ten fields
provides:
  - kMaxBitClockHz and kMinOversampleRatio named constants in TdmAnalyzerSettings.h
  - Zero-parameter guards in SetSettingsFromInterfaces() rejecting frame_rate=0, slots=0, bits=0
  - 500 MHz hard block in SetSettingsFromInterfaces() rejecting physically impossible bit clock configs
  - Advisory FrameV2 frame ("advisory" type) emitted as row 0 when capture rate < 4x bit clock
  - low_sample_rate boolean field on every slot FrameV2 row (ten-field schema)
  - Severity elevation from "ok" to "warning" when mLowSampleRate and no decode errors
affects: [07-wav-export, any future phase reading FrameV2 slot schema]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Static constexpr at file scope in header for cross-TU constants (static prevents linker multiple-definition)"
    - "FormatHzString() helper duplicated across two .cpp files (no new header) — ~8 lines, acceptable per research"
    - "Advisory FrameV2 at sample 0,0 — zero-duration point frame precedes all slot frames safely"
    - "mLowSampleRate member bridges WorkerThread() computation to AnalyzeTdmSlot() per-row emission"
    - "CommitResults() called immediately after advisory AddFrameV2 so row 0 visible before analysis completes"

key-files:
  created: []
  modified:
    - src/TdmAnalyzerSettings.h
    - src/TdmAnalyzerSettings.cpp
    - src/TdmAnalyzer.h
    - src/TdmAnalyzer.cpp
    - CHANGELOG.md

key-decisions:
  - "FormatHzString duplicated in both .cpp files rather than adding shared header — small function, cleaner than new header"
  - "Advisory frame uses sample span (0,0) — zero-duration point frame at sample 0 safely precedes all slot data"
  - "mLowSampleRate is a member variable not a local — required so WorkerThread() result flows into AnalyzeTdmSlot()"
  - "Exactly 500 MHz allowed (strict > not >=) — threshold is raw bit clock per CONTEXT.md decision"
  - "4x exactly = no warning (strict < not <=) — consistent with CONTEXT.md 4x oversample decision"
  - "Advisory is non-blocking — analysis completes normally after emitting advisory FrameV2"

patterns-established:
  - "Advisory FrameV2 pattern: zero-duration (0,0) point frame at sample 0 with type 'advisory' and severity/message fields"
  - "Cross-TU constants: static constexpr at file scope in .h (not inside class) to avoid linker multiple-definition"

requirements-completed: [SRAT-01, SRAT-02]

# Metrics
duration: 2min
completed: 2026-02-26
---

# Phase 6 Plan 01: Sample Rate Validation Summary

**Settings hard-block at 500 MHz bit clock + non-blocking advisory FrameV2 row 0 with per-slot low_sample_rate boolean when capture rate < 4x bit clock**

## Performance

- **Duration:** 2 min
- **Started:** 2026-02-26T02:32:09Z
- **Completed:** 2026-02-26T02:34:36Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Settings dialog now rejects zero-parameter configurations (frame_rate=0, slots=0, bits=0) with clear error messages before any arithmetic
- Settings dialog hard-blocks configurations requiring > 500 MHz bit clock with detailed error showing computed rate and contributing parameters
- WorkerThread emits "advisory" FrameV2 as row 0 (at sample 0) when capture rate < 4x bit clock, with human-readable math message
- Every slot FrameV2 row now includes a `low_sample_rate` boolean field (schema expanded from nine to ten fields)
- Slot severity elevated from "ok" to "warning" when `mLowSampleRate` is true and no decode errors present
- Advisory is non-blocking — analysis runs to completion after emitting the advisory row

## Task Commits

Each task was committed atomically:

1. **Task 1: Settings validation — zero-param guards and 500 MHz hard block** - `dc5cf43` (feat)
2. **Task 2: WorkerThread advisory, slot low_sample_rate boolean, severity elevation, and CHANGELOG** - `dd72935` (feat)

**Plan metadata:** *(pending final docs commit)*

## Files Created/Modified

- `src/TdmAnalyzerSettings.h` - Added `kMaxBitClockHz` (500 MHz) and `kMinOversampleRatio` (4) as static constexpr at file scope
- `src/TdmAnalyzerSettings.cpp` - Added `FormatHzString()` helper; zero-param guards and 500 MHz hard block in `SetSettingsFromInterfaces()`
- `src/TdmAnalyzer.h` - Added `bool mLowSampleRate` member variable in protected section
- `src/TdmAnalyzer.cpp` - Added `#include <stdio.h>`, `FormatHzString()` helper, advisory block in `WorkerThread()`, `low_sample_rate` field and severity elevation in `AnalyzeTdmSlot()`
- `CHANGELOG.md` - Documented SRAT-01 and SRAT-02 changes with requirement IDs in Changed/Added sections and updated Migration code block

## Decisions Made

- FormatHzString() duplicated in both .cpp files rather than adding a shared header — the function is ~8 lines and a new header would be disproportionate overhead (per research recommendation)
- Advisory FrameV2 uses sample span `(0, 0)` — zero-duration point frame at sample 0, which safely precedes all slot data since real samples start > 0
- `mLowSampleRate` stored as member variable (not local) so the result computed once in `WorkerThread()` flows into every `AnalyzeTdmSlot()` call
- Exactly 500 MHz is allowed (strict `>` not `>=`) and exactly 4x oversample rate is not flagged (strict `<` not `<=`) — both per CONTEXT.md decisions
- Advisory frame is non-blocking by design — `SetErrorText()` was explicitly ruled out; the advisory appears as row 0 in the data table

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 6 plan 01 complete — both SRAT-01 and SRAT-02 requirements satisfied
- FrameV2 schema is now ten fields: slot, data, frame_number, severity, short_slot, extra_slot, bitclock_error, missed_data, missed_frame_sync, low_sample_rate
- Advisory frame type "advisory" established — future phases must not emit conflicting frame types
- Ready for Phase 7: WAV Export

---
*Phase: 06-sample-rate-validation*
*Completed: 2026-02-26*
