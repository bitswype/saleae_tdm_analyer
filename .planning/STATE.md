---
gsd_state_version: 1.0
milestone: v1.5
milestone_name: Python HLA WAV Companion
status: unknown
last_updated: "2026-03-03T04:27:27.788Z"
progress:
  total_phases: 2
  completed_phases: 2
  total_plans: 2
  completed_plans: 2
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-02)

**Core value:** Correctly decode TDM audio data from logic analyzer captures with confidence that the results are accurate and the code is trustworthy.
**Current focus:** v1.5 — Python HLA WAV Companion (Phases 8-10)

## Current Position

Phase: 9 of 10 (Core WAV Writing) — complete
Plan: 01 of 1 complete
Status: Phase 9 Plan 01 complete — full decode() implementation with WAV writing machinery
Last activity: 2026-03-03 — hla/TdmWavExport.py fully implemented with WAV writing

Progress: [███░░░░░░░] ~67% (Phases 8+9 complete; Phase 10 remaining)

## Performance Metrics

**Velocity:**
- Total plans completed: 2 (this milestone)
- Average duration: ~2 min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 8. HLA Scaffold & Settings | 1 | 2 min | 2 min |
| 9. Core WAV Writing | 1 | 2 min | 2 min |
| 10. Error Handling & Docs | — | — | — |

## Accumulated Context

### Key Decisions

- Python HLA lives in `hla/` subdirectory alongside C++ plugin (same repo)
- Milestone version: v1.5
- HLA configured via Logic 2 settings UI (slots, output_path, bit_depth)
- WAV written continuously during decode with periodic header refresh
- Standard Python `wave` module (part of Python 3.8 stdlib embedded in Logic 2)
- Absolute paths required for output_path — relative paths resolve to Logic 2 install dir
- No sandboxing in Logic 2 HLA environment (confirmed by Saleae staff)
- apiVersion must be "1.0.0" — the only value Logic 2 accepts
- Settings declared as class-level attributes (not in __init__) so Logic 2 discovers them at load time
- bit_depth.default = '16' set post-construction; default= kwarg raises TypeError in ChoicesSetting
- entryPoint "TdmWavExport.TdmWavExport" is case-sensitive on Linux — must match .py filename exactly
- parse_slot_spec uses dict.fromkeys() not sorted(set()) — preserves user-specified channel order (REQ-09)
- _as_signed applied unconditionally — makes HLA robust regardless of LLA Signed setting
- ImportError guard added for saleae.analyzers — allows standalone python3 self-testing
- Sample rate sanity clamped to 1000-200000 Hz; falls back to 48000 for imprecise GraphTime deltas
- decode() always returns None — WAV file is the output, no HLA annotation frames generated

### Research Findings (2026-03-02)

- HLA file I/O confirmed working via Saleae forum (staff post + SaleaeSocketTransportHLA community HLA)
- Python 3.8 embedded, no sandbox, full stdlib assumed available
- No finalize/destructor hook — periodic header refresh is the correct pattern (wave.writeframes() auto-patches header)
- Automation API cannot write WAV (CSV only) — HLA is the right approach
- wave.writeframes() calls _patchheader() internally after every write — REQ-14 satisfied automatically

### Pending Todos

None.

### Blockers/Concerns

None.

## Session Continuity

Last session: 2026-03-03
Stopped at: Completed 09-01-PLAN.md — full decode() implementation with WAV writing
Resume file: —
