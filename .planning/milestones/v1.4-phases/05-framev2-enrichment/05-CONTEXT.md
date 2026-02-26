# Phase 5: FrameV2 Enrichment - Context

**Gathered:** 2026-02-25
**Status:** Ready for planning

<domain>
## Phase Boundary

Add structured boolean error fields, a severity enum, and a renamed slot identifier to every decoded TDM slot in the FrameV2 output. Enables HLA scripts to filter error frames programmatically without string parsing. Remove legacy string-based error/warning fields.

</domain>

<decisions>
## Implementation Decisions

### Field schema (replaces current FrameV2 output)
- **Remove** `channel`, `errors`, and `warnings` fields entirely
- **Rename** `channel` to `slot` — same 0-based value, new key name
- **Add** five boolean fields: `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, `missed_frame_sync`
- **Add** `severity` string field with values: `error`, `warning`, or `ok`
- **Keep** `data`, `frame_number` fields unchanged

### Column order (FrameV2 AddField sequence)
- `slot` → `data` → `frame_number` → `severity` → `short_slot` → `extra_slot` → `bitclock_error` → `missed_data` → `missed_frame_sync`
- Boolean columns ordered by expected frequency of occurrence
- All fields emitted on every slot row, including error-free rows (booleans show false, severity shows "ok")

### Severity field logic
- Any error boolean true → severity = `error`
- Only `extra_slot` true (no errors) → severity = `warning`
- All booleans false → severity = `ok`
- Error wins: if both error and warning booleans are true, severity = `error`

### Extra slot behavior
- `extra_slot` remains a warning (not promoted to error)
- Data is still decoded normally for extra slots — value is useful for debugging
- Slot number keeps incrementing beyond configured slots-per-frame (no cap/reset)
- Row coloring: DISPLAY_AS_WARNING_FLAG for warning-only, DISPLAY_AS_ERROR_FLAG wins when errors present

### Short slot behavior
- Data field stays 0 when `short_slot` is true (current behavior — no partial decode)
- `short_slot` is an error — severity = `error`, DISPLAY_AS_ERROR_FLAG set

### Frame number
- Stays 0-based, key name `frame_number` unchanged
- Extra slots stay on the same frame_number as the last valid slot in that frame

### FrameV1 (legacy)
- No changes to FrameV1 / mResultsFrame behavior — only FrameV2 output changes
- mResultsFrame.mType still carries 0-based slot number via mSlotNum
- mResultsFrame.mFlags still carries the existing flag bitmask

### Row coloring
- Preserve existing DISPLAY_AS_ERROR_FLAG and DISPLAY_AS_WARNING_FLAG behavior on Frame-level flags
- Error flag takes precedence over warning flag (current behavior, unchanged)

### Requirements updates (do during this phase)
- **Update FRM2-06**: Change from "add 1-based slot alongside channel" to "replace channel with 0-based slot"
- **Add FRM2-07**: "Replace errors/warnings string fields with severity enum field (error/warning/ok)"
- **Update Phase 5 success criteria**: Replace channel references with slot, add severity column, remove 1-based mention

### Versioning and documentation
- No version bump per-phase — accumulate to milestone end
- Breaking changes documented in CHANGELOG.md only (no README FrameV2 schema update)
- No existing HLA scripts to migrate — breaking changes are safe

### Claude's Discretion
- Exact implementation of severity string construction
- Whether to use AddBoolean vs AddInteger(0/1) for boolean fields (SDK capability dependent)
- Test approach for verifying boolean field emission

</decisions>

<specifics>
## Specific Ideas

- Column order designed for scanning: identity (slot) → payload (data) → context (frame_number) → diagnostics (severity + booleans)
- Boolean order by frequency matches real-world TDM debugging: short slots and extra slots are most common, missed frame sync is rarest

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 05-framev2-enrichment*
*Context gathered: 2026-02-25*
