# Phase 4: SDK Audit and Housekeeping - Context

**Gathered:** 2026-02-25
**Status:** Ready for planning

<domain>
## Phase Boundary

Verify the AnalyzerSDK pin is current HEAD, remove the dead `mResultsFrameV2` member variable, and rename the `"frame #"` FrameV2 key to `"frame_number"` so HLA Python scripts can access it without workarounds. This is housekeeping — no new features, no new fields.

</domain>

<decisions>
## Implementation Decisions

### Breaking change communication
- Create a new CHANGELOG.md following Keep a Changelog format
- Add a "Breaking Changes" section under v2.0.0 documenting the `"frame #"` → `"frame_number"` key rename
- Include a before/after Python migration example showing `frame.data["frame #"]` → `frame.data["frame_number"]`
- Add a migration note in README.md referencing the breaking change

### Version strategy
- Bump to **v2.0.0** (not v1.4) — the FrameV2 key rename is a breaking change for HLA scripts, warranting a major semver bump
- Tag v2.0.0 after this phase completes (not at milestone end)
- Subsequent phases (5-7) increment as v2.0.x or v2.x.0 as appropriate
- Milestone name updates from "v1.4 SDK & Export Modernization" to "v2.0 SDK & Export Modernization" across planning docs

### Claude's Discretion
- CHANGELOG.md exact structure and wording
- README migration note placement and formatting
- Whether to update ROADMAP.md/REQUIREMENTS.md references from v1.4 to v2.0 in this phase or defer to milestone-level cleanup

</decisions>

<specifics>
## Specific Ideas

- User wants semver compliance: breaking change = major version bump
- Migration example should be a concrete Python code diff, not just prose

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 04-sdk-audit-and-housekeeping*
*Context gathered: 2026-02-25*
