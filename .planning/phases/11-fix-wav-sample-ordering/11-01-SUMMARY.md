---
phase: 11-fix-wav-sample-ordering
plan: 01
subsystem: hla
tags: [bug-fix, wav-writing, tdd, req-closure]
dependency_graph:
  requires: []
  provides: [correct-wav-sample-ordering]
  affects: [hla/TdmWavExport.py]
tech_stack:
  added: []
  patterns: [tdd-red-green, self-test-in-main-block]
key_files:
  created: []
  modified:
    - hla/TdmWavExport.py
    - .planning/REQUIREMENTS.md
decisions:
  - Flush before accumulate is the correct invariant — _try_flush() clears self._accum so accumulate must run after flush to land in a clean accumulator for the new frame
  - Test added directly to existing __main__ self-test block to keep all behavioral tests co-located
metrics:
  duration: ~2 min
  completed: 2026-03-02
  tasks_completed: 2
  files_modified: 2
---

# Phase 11 Plan 01: Fix WAV Sample Ordering Summary

**One-liner:** Fixed decode() accumulate-before-flush ordering bug that caused each TDM frame's slot0 value to bleed from the next frame into the WAV output.

## What Was Built

### Bug Fix: flush-before-accumulate ordering in decode()

The root cause: in `TdmWavExport.decode()`, the sample accumulate block ran BEFORE `_try_flush()`. Since `_try_flush()` resets `self._accum = {}` on a frame boundary, any slot arriving at the boundary would write its value to `_accum`, then the flush would see the new value and write it as if it belonged to the previous frame.

**Corrected ordering in decode():**

```python
# Derive sample rate from frame timing (REQ-13)
self._try_derive_sample_rate(frame)

# Detect TDM frame boundary and flush completed frame (REQ-15).
# Flush BEFORE accumulating so the flush reads the previous frame's
# clean accumulator, not the current slot's newly-arrived data.
self._try_flush(frame_num)

# Accumulate sample AFTER flush — error frames contribute silence by
# not writing to accum; self._accum.get(slot, 0) returns 0 for them.
if not (frame.data.get('short_slot') or frame.data.get('bitclock_error')):
    self._accum[slot] = _as_signed(frame.data.get('data', 0), self._bit_depth)
```

**Effect:** Two-frame capture [TDM1: slot0=100, slot1=200] [TDM2: slot0=300, slot1=400] now produces WAV samples `[100, 200, 300, 400]` instead of the buggy `[300, 200, 999, 400]`.

### TDD Self-Test Added

A `_FakeFrame` class and full two-frame scenario test were added to the `__main__` block. The test:
- Creates a temporary WAV file
- Feeds five slot frames spanning three TDM frame numbers
- Reads back the WAV and asserts samples `== [100, 200, 300, 400]`
- Confirms frame 2 (999) is not yet flushed (no trigger after it)

### REQUIREMENTS.md Checkbox

REQ-15 updated from `[ ]` to `[x]` — the final unchecked requirement. All 22 requirements are now complete.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Reorder decode() — flush before accumulate (TDD) | 6c03fde (RED), 3bf979e (GREEN) | hla/TdmWavExport.py |
| 2 | Update REQUIREMENTS.md checkboxes REQ-08 through REQ-15 | 5c728cd | .planning/REQUIREMENTS.md |

## Verification Results

```
All self-tests passed.
REQ-15 decode ordering test passed.
```

All 22 requirements checked: `grep -c "\[x\] REQ-"` returns 22.
No unchecked requirements remain: `grep "\[ \] REQ-"` returns zero matches.

## Deviations from Plan

### Minor Deviation: REQ-08 through REQ-14 already checked

The plan stated "REQ-08 through REQ-14 show `[ ]` in the file" based on an earlier audit. On reading the actual file, all of REQ-08 through REQ-14 were already `[x]`. Only REQ-15 needed updating. Applied Rule 1 automatically — read file first, then applied only the changes that were actually needed.

All other aspects executed exactly as written.

## Self-Check: PASSED

- [x] hla/TdmWavExport.py modified with flush-before-accumulate ordering
- [x] `python3 hla/TdmWavExport.py` exits 0 and prints both expected lines
- [x] .planning/REQUIREMENTS.md updated — REQ-15 marked [x]
- [x] `grep -c "\[x\] REQ-" .planning/REQUIREMENTS.md` returns 22
- [x] Commits 6c03fde, 3bf979e, 5c728cd exist
