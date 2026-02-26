# Phase 4: SDK Audit and Housekeeping - Research

**Researched:** 2026-02-25
**Domain:** C++ Saleae AnalyzerSDK — dead code removal, FrameV2 key rename, breaking change documentation
**Confidence:** HIGH

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **Breaking change communication**: Create a new CHANGELOG.md following Keep a Changelog format. Add a "Breaking Changes" section under v2.0.0 documenting the `"frame #"` → `"frame_number"` key rename. Include a before/after Python migration example showing `frame.data["frame #"]` → `frame.data["frame_number"]`. Add a migration note in README.md referencing the breaking change.
- **Version strategy**: Bump to **v2.0.0** (not v1.4) — the FrameV2 key rename is a breaking change for HLA scripts, warranting a major semver bump. Tag v2.0.0 after this phase completes (not at milestone end). Subsequent phases (5-7) increment as v2.0.x or v2.x.0 as appropriate. Milestone name updates from "v1.4 SDK & Export Modernization" to "v2.0 SDK & Export Modernization" across planning docs.

### Claude's Discretion

- CHANGELOG.md exact structure and wording
- README migration note placement and formatting
- Whether to update ROADMAP.md/REQUIREMENTS.md references from v1.4 to v2.0 in this phase or defer to milestone-level cleanup

### Deferred Ideas (OUT OF SCOPE)

None — discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| SDKM-01 | Audit AnalyzerSDK pin — verify 114a3b8 is latest HEAD, document finding in commit message | Confirmed by `git ls-remote` on 2026-02-25: `114a3b8306e6a5008453546eda003db15b002027` is still HEAD of master. No code changes required. Commit message is the deliverable. |
| SDKM-02 | Remove unused `mResultsFrameV2` member variable from TdmAnalyzer.h | Member is at TdmAnalyzer.h line 40. AnalyzeTdmSlot() uses a local `FrameV2 frame_v2` variable (TdmAnalyzer.cpp line 299). The member is never read, written, or passed anywhere. Safe to remove with no other changes. |
| SDKM-03 | Rename FrameV2 key `"frame #"` to `"frame_number"` — space and hash in key breaks HLA Python attribute-style access | Key is written at TdmAnalyzer.cpp line 336: `frame_v2.AddInteger("frame #", mFrameNum)`. Single-line change. This is the breaking change that drives the v2.0.0 version bump and requires CHANGELOG.md + README migration note. |
</phase_requirements>

## Summary

Phase 4 is the smallest and lowest-risk phase in the v1.4/v2.0 milestone. It has three tasks: verify the SDK pin, remove a dead member variable, and rename one FrameV2 key. The total code diff is three lines changed across two source files plus new documentation files.

The FrameV2 key rename (`"frame #"` to `"frame_number"`) is the only breaking change — any existing HLA Python script that accesses `frame.data["frame #"]` will raise a `KeyError` after this release. This warrants the v2.0.0 version bump per user decision. The CHANGELOG.md and README migration note are required deliverables that make the break explicit and provide a migration path.

The SDK audit result is confirmed: `114a3b8` is still the HEAD of the `saleae/AnalyzerSDK` master branch as of 2026-02-25. This has been verified twice (2026-02-24 during v1.4 research, and 2026-02-25 during this phase research) via `git ls-remote`. No SDK code change is needed — the deliverable is a commit message documenting the audit date and result.

**Primary recommendation:** Execute all three requirements plus documentation work in a single atomic plan (04-01). The changes are small, fully understood, and have no dependencies on each other or on subsequent phases. Ship CHANGELOG.md, README update, dead member removal, and key rename together, then tag v2.0.0.

## Standard Stack

### Core

| Component | Version / Location | Purpose | Why Standard |
|-----------|-------------------|---------|--------------|
| AnalyzerSDK | `114a3b8` (confirmed HEAD 2026-02-25) | Plugin base classes, FrameV2 API | Already pinned; no update exists |
| C++11 | Mandated by CMakeLists.txt (`CMAKE_CXX_STANDARD 11`) | Project language standard | SDK requires C++11; no change needed |
| Keep a Changelog | keepachangelog.com format | CHANGELOG.md structure | User-specified format |
| Semantic Versioning | semver.org (MAJOR.MINOR.PATCH) | Version numbering | User-specified; breaking change = major bump |

### Supporting

| Component | Location | Purpose | When to Use |
|-----------|----------|---------|-------------|
| git tag | CLI | Create annotated v2.0.0 tag | After all phase commits are merged to main |
| README.md | Repo root | User-facing documentation | Migration note for HLA script authors |
| CHANGELOG.md | Repo root (new file) | Breaking change communication | Created in this phase |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `"frame_number"` key name | `"frame_num"` | Both are valid. `"frame_number"` chosen because SUMMARY.md resolved the gap (check Saleae CAN analyzer naming: full words like `"identifier"`, `"num_data_bytes"`). User decision says `"frame_number"`. |

## Architecture Patterns

### Recommended Project Structure

No new directories or files needed in src/. New files at repo root only:

```
repo root
├── CHANGELOG.md           (NEW — Keep a Changelog format)
├── README.md              (MODIFY — add migration note)
src/
├── TdmAnalyzer.h          (MODIFY — remove mResultsFrameV2 line 40)
├── TdmAnalyzer.cpp        (MODIFY — rename "frame #" to "frame_number" line 336)
cmake/
└── ExternalAnalyzerSDK.cmake   (NO CHANGE — already at HEAD)
```

### Pattern 1: SDK Audit Documentation via Commit Message

**What:** The audit deliverable is a commit message, not a source code change. The commit contains the verification command, the result, and the date.

**When to use:** When the audit result is a no-op (pin is already current) and the requirement is to document that the audit was performed.

**Example commit message:**
```
chore: SDK audit — 114a3b8 confirmed current HEAD (2026-02-25)

AnalyzerSDK HEAD verified via:
  git ls-remote https://github.com/saleae/AnalyzerSDK.git HEAD

Result: 114a3b8306e6a5008453546eda003db15b002027

This is the same commit pinned in cmake/ExternalAnalyzerSDK.cmake.
No SDK update is needed. All SDK APIs are used correctly:
- UseFrameV2() called in TdmAnalyzer() constructor
- AddFrameV2() called before CommitResults() in AnalyzeTdmSlot()
- ClearTabularText() first call in GenerateFrameTabularText()

Closes SDKM-01.
```

### Pattern 2: Dead Member Removal in C++ Header

**What:** Remove a single member variable declaration from a class. The member `mResultsFrameV2` at `TdmAnalyzer.h` line 40 is a `FrameV2` type object declared in the protected section. It is never referenced in any .cpp file — `AnalyzeTdmSlot()` allocates a local `FrameV2 frame_v2` on the stack instead.

**Verification:** After removing the line, the project must compile cleanly. No `mResultsFrameV2` references exist anywhere in the codebase.

**Current state (TdmAnalyzer.h lines 38-41):**
```cpp
    Frame mResultsFrame;
    FrameV2 mResultsFrameV2;    // <- REMOVE THIS LINE
    bool mSimulationInitilized;
```

**After removal:**
```cpp
    Frame mResultsFrame;
    bool mSimulationInitilized;
```

### Pattern 3: FrameV2 Key Rename (Single-Line Change)

**What:** Change the string literal `"frame #"` to `"frame_number"` in one `AddInteger()` call.

**Location:** `TdmAnalyzer.cpp` line 336:
```cpp
// BEFORE:
frame_v2.AddInteger("frame #", mFrameNum);

// AFTER:
frame_v2.AddInteger("frame_number", mFrameNum);
```

**Context:** This call is inside `AnalyzeTdmSlot()`, within the FrameV2 block that runs between `AddFrame()` and `CommitResults()`. The surrounding code does not change.

### Pattern 4: Keep a Changelog Format

**What:** CHANGELOG.md follows the keepachangelog.com convention with unreleased changes at top, versions as H2 headers, and standard sections (Added, Changed, Removed, Fixed, Breaking Changes).

**Example CHANGELOG.md structure for this phase:**
```markdown
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-02-25

### Breaking Changes

- **FrameV2 key rename**: The `"frame #"` field key has been renamed to `"frame_number"`.
  HLA scripts that access this field must be updated.

  **Before:**
  ```python
  frame_number = frame.data["frame #"]
  ```

  **After:**
  ```python
  frame_number = frame.data["frame_number"]
  ```

### Changed

- Renamed FrameV2 field key `"frame #"` to `"frame_number"` for HLA compatibility.
  Keys with spaces or special characters can break Python attribute-style access
  and may cause issues with future Saleae tooling.

### Removed

- Removed unused `mResultsFrameV2` member variable from `TdmAnalyzer` class.
  This was dead code — the implementation correctly uses a local `FrameV2` variable
  in `AnalyzeTdmSlot()`.

[2.0.0]: https://github.com/your-org/saleae-tdm-analyzer/releases/tag/v2.0.0
```

### Pattern 5: README Migration Note

**What:** Add a short section to README.md near the HLA/FrameV2 documentation (or near the top as a migration notice if the break is prominent). The note must reference CHANGELOG.md.

**Recommended placement:** A "Migration Guide" or "Breaking Changes" section, or a callout block near the FrameV2 field documentation.

**Example content:**
```markdown
## Migration Guide

### v2.0.0 — FrameV2 key rename

The `"frame #"` FrameV2 field has been renamed to `"frame_number"`.
Update any HLA scripts that access this field:

```python
# Before (v1.x)
frame_number = frame.data["frame #"]

# After (v2.0+)
frame_number = frame.data["frame_number"]
```

See [CHANGELOG.md](CHANGELOG.md) for the full list of changes.
```

### Pattern 6: Annotated Git Tag for v2.0.0

**What:** After all phase commits land on main, create an annotated tag (not a lightweight tag) so the tag message documents what the release contains.

**Command:**
```bash
git tag -a v2.0.0 -m "v2.0.0 — SDK audit, dead code removal, FrameV2 key rename (breaking)"
git push origin v2.0.0
```

**Why annotated:** The project's global CLAUDE.md references `git describe --tags --always --dirty` for version display. Annotated tags are required for `git describe` to produce meaningful output.

### Anti-Patterns to Avoid

- **Combining SDK audit commit with source code changes:** Keep the SDK audit commit (SDKM-01) and the code changes (SDKM-02, SDKM-03) separate. The audit commit proves the SDK was checked; mixing it with source changes obscures the audit record.
- **Lightweight git tag:** A lightweight tag (`git tag v2.0.0`) lacks a tag message and may not be picked up by `git describe` on all platforms as reliably as an annotated tag.
- **Updating REQUIREMENTS.md v1.4 references in this phase:** This is Claude's discretion territory. Defer the milestone-wide rename from v1.4 to v2.0 to a separate cleanup commit or the final milestone wrap-up to avoid scope creep in this phase.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Changelog format | Custom markdown format | Keep a Changelog (keepachangelog.com) | Standard format tools and readers expect; user-specified |
| Version numbering scheme | Ad-hoc versioning | Semantic Versioning (semver.org) | User-specified; major bump for breaking changes |
| Git version tag | Non-annotated lightweight tag | `git tag -a` annotated tag | Required for `git describe` to work correctly per project global CLAUDE.md |

## Common Pitfalls

### Pitfall 1: Forgetting UseFrameV2() Is Still Required After mResultsFrameV2 Removal

**What goes wrong:** Developer removes `mResultsFrameV2` and then searches for `FrameV2` in the header, finds nothing, and wonders if FrameV2 is still configured — then removes `UseFrameV2()` from the constructor.

**Why it happens:** The dead member and the required constructor call both involve `FrameV2` as a type. Their relationship is not obvious.

**How to avoid:** After removing `mResultsFrameV2` from the header, verify `UseFrameV2()` is still present in `TdmAnalyzer::TdmAnalyzer()` in `TdmAnalyzer.cpp` (line 13). Do not touch the constructor. Optionally add a comment above `UseFrameV2()`:
```cpp
// Required: registers this analyzer as a FrameV2 producer.
// Without this, AddFrameV2() data is silently dropped and the data table will be empty.
// Do not remove. Requires Logic 2.3.43+.
UseFrameV2();
```

**Warning signs:** After building, the Logic 2 data table shows no rows despite the analyzer running.

### Pitfall 2: Wrong Key Name Chosen (`"frame_num"` vs `"frame_number"`)

**What goes wrong:** Prior research in PITFALLS.md used `"frame_num"` as the recommended key; ARCHITECTURE.md and the user decision use `"frame_number"`. Using `"frame_num"` would misalign with the documented user decision.

**Why it happens:** Two research documents used slightly different recommendations before the user resolved the ambiguity.

**How to avoid:** The user decision in CONTEXT.md is authoritative: use `"frame_number"`. This matches Saleae's CAN analyzer field naming pattern (`"identifier"`, `"num_data_bytes"` — full words, underscore-separated). The commit, code, CHANGELOG.md, and README must all use `"frame_number"` consistently.

### Pitfall 3: CHANGELOG.md Missing a Concrete Python Code Diff

**What goes wrong:** The breaking change section describes the rename in prose but omits the before/after Python example. HLA authors may not understand what to change.

**Why it happens:** Documentation writers often describe what changed rather than showing what to do.

**How to avoid:** The user decision explicitly requires "a before/after Python migration example showing `frame.data["frame #"]` → `frame.data["frame_number"]`". The code diff must appear in the CHANGELOG.md Breaking Changes section, not just in README.md.

### Pitfall 4: Creating CHANGELOG.md Without the v2.0.0 Link at the Bottom

**What goes wrong:** Keep a Changelog format requires comparison links at the bottom of the file (e.g., `[2.0.0]: https://...`). Omitting them is technically valid but breaks the format convention.

**Why it happens:** The link section is easy to forget.

**How to avoid:** Include the link section even if the GitHub URL is a placeholder. The URL can be updated to the actual release URL when the tag is pushed.

### Pitfall 5: Tagging Before All Phase Commits Land

**What goes wrong:** Tag v2.0.0 is created after the SDK audit commit but before the SDKM-02 and SDKM-03 commits. The tag points to an incomplete state.

**Why it happens:** The SDK audit commit is the first commit in the phase and could feel "complete."

**How to avoid:** Create the v2.0.0 tag only after all three requirements (SDKM-01, SDKM-02, SDKM-03) are committed and merged to main.

## Code Examples

Verified patterns from source inspection:

### SDKM-02: Remove Dead Member (TdmAnalyzer.h)

```cpp
// Source: src/TdmAnalyzer.h lines 38-41 (current state)
  protected:
    std::unique_ptr<TdmAnalyzerSettings> mSettings;
    std::unique_ptr<TdmAnalyzerResults> mResults;
    U64 mSampleRate;
    double mDesiredBitClockPeriod;
    Frame mResultsFrame;
    FrameV2 mResultsFrameV2;     // <- REMOVE THIS LINE (dead code)
    bool mSimulationInitilized;
```

### SDKM-03: Key Rename (TdmAnalyzer.cpp)

```cpp
// Source: src/TdmAnalyzer.cpp lines 333-337 (current state)
    frame_v2.AddString("errors", error_str);
    frame_v2.AddString("warnings", warning_str);
    frame_v2.AddInteger("frame #", mFrameNum);   // <- CHANGE TO "frame_number"
    mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
```

```cpp
// After rename:
    frame_v2.AddString("errors", error_str);
    frame_v2.AddString("warnings", warning_str);
    frame_v2.AddInteger("frame_number", mFrameNum);
    mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
```

### UseFrameV2() Comment Guard (TdmAnalyzer.cpp)

```cpp
// Source: src/TdmAnalyzer.cpp lines 7-14 (current state — keep, optionally add comment)
TdmAnalyzer::TdmAnalyzer()
  : Analyzer2(),
    mSettings( new TdmAnalyzerSettings() ),
    mSimulationInitilized( false )
{
    SetAnalyzerSettings( mSettings.get() );
    // Required: registers this analyzer as a FrameV2 producer.
    // Without this, AddFrameV2() data is silently dropped.
    // Do not remove. Requires Logic 2.3.43+.
    UseFrameV2();
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `"frame #"` FrameV2 key (space + hash) | `"frame_number"` (clean underscore key) | Phase 4 (this phase) | HLA scripts can access without workarounds; aligns with Saleae naming conventions |
| `mResultsFrameV2` dead member in header | Removed — local `frame_v2` in `AnalyzeTdmSlot()` only | Phase 4 (this phase) | Cleaner class definition; no confusion about which object is used |
| No CHANGELOG.md | CHANGELOG.md (Keep a Changelog format) | Phase 4 (this phase) | Breaking changes documented; migration path explicit |
| v1.x versioning | v2.0.0 (major version for breaking change) | Phase 4 (this phase) | Semantic versioning communicates breakage to users |

**Deprecated/outdated:**
- `"frame #"` as a FrameV2 key: replaced by `"frame_number"` in this phase. Any HLA script using `frame.data["frame #"]` must update.

## Open Questions

1. **Whether to update v1.4 references to v2.0 in REQUIREMENTS.md/ROADMAP.md during this phase**
   - What we know: User said "Claude's Discretion" on whether to update planning doc references now or defer.
   - What's unclear: Whether updating now or deferring causes less confusion for subsequent phases.
   - Recommendation: Update REQUIREMENTS.md title from "Requirements: TDM Analyzer v1.4" to "v2.0" in this phase — it is a two-word change and the requirements doc will be read by subsequent phase plans. Defer ROADMAP.md to milestone-level cleanup. This is a minimal edit that prevents confusion without significant scope creep.

2. **CHANGELOG.md GitHub URL placeholder**
   - What we know: The repository URL is not documented in any planning file. The current remote is not checked.
   - What's unclear: The actual GitHub/GitLab URL for the release link at the bottom of CHANGELOG.md.
   - Recommendation: Use a placeholder URL (e.g., `https://github.com/owner/saleae_tdm_analyzer/releases/tag/v2.0.0`) and update when the tag is pushed. Alternatively, check `git remote -v` at plan execution time to insert the real URL.

## Sources

### Primary (HIGH confidence)

- `src/TdmAnalyzer.h` — direct inspection; `mResultsFrameV2` at line 40 confirmed unused
- `src/TdmAnalyzer.cpp` — direct inspection; `"frame #"` key at line 336 confirmed; `UseFrameV2()` at line 13 confirmed
- `git ls-remote https://github.com/saleae/AnalyzerSDK.git HEAD` — executed 2026-02-25; result: `114a3b8306e6a5008453546eda003db15b002027` matches pinned SHA
- `.planning/phases/04-sdk-audit-and-housekeeping/04-CONTEXT.md` — user decisions: v2.0.0, CHANGELOG.md, README migration note
- `.planning/research/SUMMARY.md` — resolved the `"frame_num"` vs `"frame_number"` ambiguity: `"frame_number"` chosen per Saleae CAN analyzer naming conventions
- `.planning/research/ARCHITECTURE.md` — confirmed mResultsFrameV2 dead member, confirmed FrameV2 block location

### Secondary (MEDIUM confidence)

- `.planning/research/PITFALLS.md` — Pitfall 4 (UseFrameV2 must not be removed during SDK work); Pitfall 5 (field key naming conventions)
- `https://keepachangelog.com/en/1.0.0/` — Keep a Changelog format reference
- `https://semver.org/spec/v2.0.0.html` — Semantic Versioning: major version bump for breaking changes

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — SDK audit result confirmed by live `git ls-remote`; all source locations verified by direct file inspection
- Architecture: HIGH — all three requirement changes are single-line or single-declaration edits; locations confirmed at specific line numbers
- Pitfalls: HIGH — grounded in direct source inspection and prior milestone research; no speculative claims

**Research date:** 2026-02-25
**Valid until:** This research does not expire — the phase changes are fully specified from source. The SDK HEAD check is valid until a new commit appears on `saleae/AnalyzerSDK` master (unlikely given the history).
