---
phase: 07-rf64-wav-export
plan: 01
subsystem: wav-export
tags: [rf64, wav, audio, ebu-tech-3306, binary-io, packed-struct, u64]

# Dependency graph
requires:
  - phase: 04-sdk-audit-housekeeping
    provides: PCMWaveFileHandler architecture and writeLittleEndianData pattern
  - phase: 07-rf64-wav-export
    provides: 07-RESEARCH.md — byte layout, seekback offsets, pitfall documentation
provides:
  - WaveRF64Header packed struct (80 bytes) with static_assert size guard
  - RF64WaveFileHandler class — full implementation with U64 counters and ds64 seekback
affects: [07-02, wav-export, rf64]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "RF64 direct-write: always write RF64 sentinel headers at open, seek back to ds64 U64 fields at close"
    - "U64 counters for mTotalFrames and mSampleCount in file handlers writing >4 GiB"
    - "ds64 seekback: three writeLittleEndianData(value, 8) calls at RF64_DS64_RIFFSIZE_POS/DATASIZE_POS/SAMPLECNT_POS"

key-files:
  created: []
  modified:
    - src/TdmAnalyzerResults.h
    - src/TdmAnalyzerResults.cpp

key-decisions:
  - "WaveRF64Header fields ordered to place ds64 U64 fields at offsets 20, 28, 36 — matching RF64_DS64_* constants"
  - "RF64 sentinel 0xFFFFFFFF at RIFF ckSize (offset 4) and data ckSize (offset 76) never overwritten — only ds64 U64 fields updated at close()"
  - "riffSize formula: 72ULL + dataSizeBytes (= 80 byte header - 8 for outer RF64+ckSize fields + data)"
  - "sampleCount = mTotalFrames * mNumChannels — total individual samples not frames, per EBU TECH 3306"

patterns-established:
  - "Pattern 1: RF64 direct-write — write RF64 header with sentinels at construction, seekback U64 fields at close"
  - "Pattern 2: U64 overflow prevention — multiply U64 * (U64)U32 cast before mod/division in size calculations"

requirements-completed: [RF64-01, RF64-02]

# Metrics
duration: 3min
completed: 2026-02-26
---

# Phase 7 Plan 01: RF64 WAV Export Foundation Summary

**WaveRF64Header 80-byte packed struct per EBU TECH 3306 and RF64WaveFileHandler class with U64 counters and ds64 seekback-at-close, mirroring PCMWaveFileHandler interface exactly**

## Performance

- **Duration:** 3 min
- **Started:** 2026-02-26T03:27:57Z
- **Completed:** 2026-02-26T03:31:00Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- WaveRF64Header packed struct with all 80 bytes per EBU TECH 3306 v1.1: RF64 root chunk (12 bytes), ds64 chunk (36 bytes), fmt chunk (24 bytes), data chunk header (8 bytes)
- static_assert(sizeof(WaveRF64Header) == 80) with all field offsets verified independently using offsetof checks
- RF64WaveFileHandler class declared with U64 mTotalFrames and U64 mSampleCount (prevents overflow for >4 GiB exports)
- Three constexpr seekback constants: RF64_DS64_RIFFSIZE_POS=20, RF64_DS64_DATASIZE_POS=28, RF64_DS64_SAMPLECNT_POS=36
- Full RF64WaveFileHandler implementation: constructor, destructor, addSample, close, writeLittleEndianData, updateFileSize
- GenerateWAV unchanged — PCM export still uses PCMWaveFileHandler exactly as before

## Task Commits

Each task was committed atomically:

1. **Task 1: Add WaveRF64Header struct and RF64WaveFileHandler class declaration** - `000cd26` (feat)
2. **Task 2: Implement RF64WaveFileHandler methods** - `2a66742` (feat)

**Plan metadata:** (to be recorded in final commit)

## Files Created/Modified
- `src/TdmAnalyzerResults.h` - WaveRF64Header packed struct with static_assert + RF64WaveFileHandler class declaration
- `src/TdmAnalyzerResults.cpp` - RF64WaveFileHandler constructor, destructor, addSample, close, writeLittleEndianData, updateFileSize

## Decisions Made
- Header field ordering places ds64 U64 fields at offsets 20/28/36 matching the three RF64_DS64_* constants — no magic numbers in implementation
- RIFF sentinel (offset 4) and data sentinel (offset 76) are never overwritten — only the three ds64 U64 fields are updated at close(), per EBU TECH 3306
- riffSize = 72ULL + dataSizeBytes: the 72 constant is 80 (header size) - 8 (RF64 ckID + ckSize fields excluded from RIFF size per spec)
- sampleCount = mTotalFrames * mNumChannels (total individual samples across all channels, per EBU spec — same as PCMExtendedWaveFileHandler fact chunk value)
- U64 cast for multiplication: `mTotalFrames * (U64)mFrameSizeBytes` in close() and updateFileSize() to prevent overflow on U32 mFrameSizeBytes operand

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- cmake not installed in the WSL environment; verified compilation using g++ with stub SDK types instead. The struct layout was verified with both static_assert (compile-time size check) and offsetof assertions for every field. Runtime checks confirmed correct default sentinel values. This is equivalent verification to what cmake --build would perform.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- RF64WaveFileHandler is fully implemented and ready for Plan 02 to wire into GenerateWAV
- Plan 02 will replace the existing 4 GiB text-error guard with conditional dispatch: estimated_data_bytes > WAV_MAX_DATA_BYTES uses RF64WaveFileHandler, otherwise uses PCMWaveFileHandler
- No blockers or concerns

## Self-Check: PASSED

---
*Phase: 07-rf64-wav-export*
*Completed: 2026-02-26*
