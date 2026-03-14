---
phase: 09-core-wav-writing
plan: 01
subsystem: hla
tags: [python, wave, struct, hla, saleae, wav, pcm, tdm]

# Dependency graph
requires:
  - phase: 08-hla-scaffold-settings
    provides: TdmWavExport class scaffold with settings, extension.json, no-op decode()
provides:
  - Full decode() implementation with slot filtering, sample accumulation, WAV writing
  - parse_slot_spec() module-level helper (insertion-order-preserving, deduplicated)
  - _as_signed() module-level helper for two's complement sign clamping
  - Four private methods: _open_wav, _try_derive_sample_rate, _write_wav_frame, _try_flush
  - Lazy WAV file open on first frame, header auto-patched by wave.writeframes()
affects: [10-error-handling-docs]

# Tech tracking
tech-stack:
  added: [wave (stdlib), struct (stdlib)]
  patterns:
    - Lazy WAV file open — file not created until first slot frame arrives
    - Frame accumulator dict (keyed by slot) flushed on frame_number boundary
    - Sample rate derived from consecutive same-slot frame timestamps via GraphTime subtraction
    - wave.writeframes() auto-patches header after every call (no manual seek needed)
    - try/except ImportError guard for saleae.analyzers to enable plain python3 self-testing

key-files:
  created: []
  modified:
    - hla-wav-export/TdmWavExport.py

key-decisions:
  - "Import guard (try/except ImportError) added for saleae.analyzers — allows self-test via plain python3 without Logic 2's embedded runtime"
  - "parse_slot_spec uses dict.fromkeys() not sorted(set()) — preserves user-specified channel order per REQ-09"
  - "_as_signed applied unconditionally — makes HLA robust regardless of LLA Signed setting"
  - "Sample rate sanity clamped to 1000-200000 Hz range; values outside fall back to 48000 to handle imprecise GraphTime deltas"
  - "decode() returns None for all frames — WAV file is the output, no HLA annotation frames generated"

patterns-established:
  - "Frame accumulator pattern: dict[slot -> sample] keyed by slot index, flushed on frame_number change"
  - "Lazy WAV lifecycle: _wav=None until first flush, _open_wav() called once when sample_rate known"
  - "Silence for missing slots: self._accum.get(slot, 0) for all slots in self._slot_list"

requirements-completed: [REQ-08, REQ-09, REQ-10, REQ-11, REQ-12, REQ-13, REQ-14, REQ-15]

# Metrics
duration: 2min
completed: 2026-03-03
---

# Phase 9 Plan 01: Core WAV Writing Summary

**Full decode() implementation with lazy WAV open, slot accumulator, frame-boundary flush, and auto-patching headers via stdlib wave module**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-03T04:22:56Z
- **Completed:** 2026-03-03T04:25:12Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments

- Implemented parse_slot_spec() and _as_signed() as module-level helpers with comprehensive self-tests covering order preservation, deduplication, whitespace tolerance, and error cases
- Implemented four private WAV lifecycle methods (_open_wav, _try_derive_sample_rate, _write_wav_frame, _try_flush) satisfying REQ-12 through REQ-15
- Replaced Phase 8 no-op decode() with full implementation: slot filter, accumulation, sample rate derivation, TDM frame boundary detection and flush

## Task Commits

Each task was committed atomically:

1. **Task 1+2: Module-level helpers, __init__ state, WAV lifecycle methods, decode()** - `71bab79` (feat)

Note: Both tasks were implemented in a single write operation, so they share one commit. The implementation is complete and all done criteria for both tasks are satisfied.

**Plan metadata:** (docs commit — see below)

## Files Created/Modified

- `hla-wav-export/TdmWavExport.py` - Full Phase 9 implementation: parse_slot_spec, _as_signed, _open_wav, _try_derive_sample_rate, _write_wav_frame, _try_flush, complete decode(), ImportError guard for standalone testing

## Decisions Made

- Added try/except ImportError guard around the saleae.analyzers import — the plan's verify step calls `python3 hla-wav-export/TdmWavExport.py` outside Logic 2's embedded runtime, so stubs are needed for standalone self-testing. Stubs are minimal and only activate when the real module is absent.
- Used dict.fromkeys() instead of sorted(set()) for deduplication in parse_slot_spec — preserves the user's channel ordering (REQ-09) rather than sorting ascending.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added ImportError guard for saleae.analyzers imports**
- **Found during:** Task 1 (parse_slot_spec and _as_signed)
- **Issue:** The plan's automated verify step (`python3 hla-wav-export/TdmWavExport.py`) runs outside Logic 2's embedded Python runtime, where `saleae.analyzers` is not available, causing ModuleNotFoundError before any test could run
- **Fix:** Added try/except ImportError block at the top of the file with minimal stubs (HighLevelAnalyzer, AnalyzerFrame, StringSetting, ChoicesSetting) so the module can be imported and self-tests executed with plain python3
- **Files modified:** hla-wav-export/TdmWavExport.py
- **Verification:** `python3 hla-wav-export/TdmWavExport.py` exits 0 with "All self-tests passed."
- **Committed in:** 71bab79 (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Auto-fix required for the plan's own verify step to work. No scope creep — stubs are only used during standalone testing, not at Logic 2 runtime.

## Issues Encountered

None beyond the saleae import issue documented above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 9 Plan 01 complete — hla-wav-export/TdmWavExport.py is now a functional Logic 2 HLA that writes multi-channel PCM WAV files
- Phase 10 (Error Handling & Docs) can proceed — all core WAV writing logic is in place
- Open concerns noted in research: sample rate precision at very high sample rates (GraphTime float() precision), and the last partial TDM frame at capture end is silently dropped (documented and accepted per research notes)

---
*Phase: 09-core-wav-writing*
*Completed: 2026-03-03*
