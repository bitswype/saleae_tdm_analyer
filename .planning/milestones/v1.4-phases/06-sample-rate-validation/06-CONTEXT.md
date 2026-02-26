# Phase 6: Sample Rate Validation - Context

**Gathered:** 2026-02-25
**Status:** Ready for planning

<domain>
## Phase Boundary

Two guardrails for TDM analysis: a non-blocking advisory warning when the capture sample rate is below 4x the bit clock rate, and a hard block in the settings dialog when configured TDM parameters require a bit clock rate exceeding 500 MHz. Analysis still runs with the soft warning; impossible configurations are rejected before analysis starts.

</domain>

<decisions>
## Implementation Decisions

### Warning annotation format
- Use a **distinct FrameV2 type** (e.g., "advisory") separate from the "slot" frame type — not the nine-field slot schema
- Severity field: `warning` (reuses Phase 5 severity enum values)
- **Show the math** in the message: actual capture rate, recommended minimum (4x bit clock), and computed bit clock rate
- Include a **brief explanation** of why 4x matters (edge detection reliability)
- Emitted **before the first decoded slot** — row 0 in the Logic 2 data table

### Error message wording
- **Show the math** with breakdown: computed bit clock, the parameters that produce it, and the 500 MHz ceiling
- Include **specific suggestions**: "Reduce frame rate, slots per frame, or bits per slot"
- Hard block is based on **raw bit clock rate** (frame_rate x slots x bits_per_slot), not the 4x oversampled rate

### Threshold behavior
- Soft warning: fires when `sample_rate < 4 * bit_clock` — exactly 4x = no warning (strict `<`)
- Hard block: fires when `bit_clock > 500 MHz` — exactly 500 MHz is allowed (strict `>`)
- **Named constants** for both thresholds: `kMinOversampleRatio = 4` and `kMaxBitClockHz = 500'000'000` (or similar)
- **U64 arithmetic** for bit clock calculation to prevent overflow with large parameter combinations
- **Auto-scale units** in messages based on magnitude (Hz, kHz, MHz)

### Settings validation additions
- **Zero-parameter guard**: reject frame_rate = 0, slots_per_frame = 0, or bits_per_slot = 0 in SetSettingsFromInterfaces
- Zero-parameter checks go **before existing validations** (fail on most basic error first)
- 500 MHz bit clock check goes **after** parameter extraction and existing validations

### Warning persistence
- Advisory frame appears as **data table row only** — no waveform markers
- **`low_sample_rate` boolean** added to every slot frame (true when capture rate < 4x bit clock)
- When `low_sample_rate` is true and no decode errors are present, slot **severity is elevated to "warning"** (not "ok")

### Claude's Discretion
- Exact advisory frame field names beyond severity (message text field, etc.)
- Formatting helper for auto-scaled unit display
- Whether the advisory type string is "advisory", "warning", or something else
- Sample span for the advisory frame (what sample range it covers)

</decisions>

<specifics>
## Specific Ideas

- Warning message should read something like: "Capture rate: 10 MSPS is below recommended 4x bit clock (48 MSPS). 4x oversampling is needed for reliable edge detection."
- Error message should read something like: "TDM configuration requires 768 MHz bit clock (48 kHz x 8 slots x 32 bits), exceeding maximum 500 MHz. Reduce frame rate, slots per frame, or bits per slot."

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 06-sample-rate-validation*
*Context gathered: 2026-02-25*
