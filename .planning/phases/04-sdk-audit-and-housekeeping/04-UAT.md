---
status: complete
phase: 04-sdk-audit-and-housekeeping
source: 04-01-SUMMARY.md
started: 2026-02-25T00:00:00Z
updated: 2026-02-25T00:01:00Z
---

## Current Test

[testing complete]

## Tests

### 1. SDK Audit Commit
expected: `git log --oneline --all | grep "SDK audit"` shows an empty commit documenting that 114a3b8 is confirmed as AnalyzerSDK HEAD with today's date (2026-02-25).
result: pass

### 2. Dead Member Removed
expected: `grep "mResultsFrameV2" src/TdmAnalyzer.h` returns no matches. The dead `FrameV2 mResultsFrameV2;` member variable is gone from the class declaration. `UseFrameV2()` in the constructor is still present (unrelated required SDK call).
result: pass

### 3. FrameV2 Key Renamed
expected: `grep "frame_number" src/TdmAnalyzer.cpp` shows `frame_v2.AddInteger("frame_number", mFrameNum)`. The old key `"frame #"` no longer appears anywhere in the file.
result: pass

### 4. CHANGELOG.md Format and Content
expected: CHANGELOG.md exists at repo root. It follows Keep a Changelog format with a `[2.0.0]` entry containing a "Breaking Changes" section. The breaking change includes a before/after Python migration example showing `frame.data["frame #"]` → `frame.data["frame_number"]`.
result: pass

### 5. README Migration Guide
expected: README.md contains a "Migration Guide" section placed prominently (before Features). It documents the `"frame #"` → `"frame_number"` rename with a Python code example and links to CHANGELOG.md.
result: pass

### 6. Annotated v2.0.0 Tag
expected: `git describe --tags` returns `v2.0.0` (or `v2.0.0-N-gXXXXXXX` if commits were added after the tag). `git tag -l v2.0.0` confirms the tag exists. The tag is annotated (not lightweight).
result: pass

## Summary

total: 6
passed: 6
issues: 0
pending: 0
skipped: 0

## Gaps

[none]
