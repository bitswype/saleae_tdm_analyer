# Requirements: v1.5 Python HLA WAV Companion

## Milestone Goal

Add a Python High Level Analyzer (HLA) to the repository that sits on top of the TDM LLA and writes a WAV file containing only the user-selected slots. The HLA lives in `hla/` alongside the C++ plugin and is installed separately in Logic 2.

## Context

- HLAs in Logic 2 run standard Python 3.8 (embedded, no sandboxing)
- HLAs receive FrameV2 frames from the LLA via their `decode()` method
- File I/O works via standard Python `open()` / `wave` module using absolute paths
- No guaranteed destructor/finalize hook — WAV header must be refreshed periodically
- HLA settings are exposed in the Logic 2 analyzer panel UI
- The TDM LLA already emits a ten-field FrameV2 schema with `slot`, `data`, `frame_number`, `severity`, and boolean error flags

## Requirements

### Installation & Structure

- REQ-01: HLA lives in `hla/` subdirectory of this repository
- REQ-02: `hla/` contains a valid `extension.json` so Logic 2 can load it as a custom extension
- REQ-03: User installs by adding `hla/` as a custom extension directory in Logic 2 preferences
- REQ-04: HLA appears in Logic 2 as "TDM WAV Export" in the analyzer chain

### Settings UI

- REQ-05: HLA exposes a `slots` setting — comma-separated or range notation (e.g. `0,2,4` or `0-3`)
- REQ-06: HLA exposes an `output_path` setting — absolute path to the desired WAV file
- REQ-07: HLA exposes a `bit_depth` setting — 16 or 32 bit (default 16)

### Slot Filtering

- REQ-08: HLA parses the `slots` setting and filters incoming frames to the specified slot numbers
- REQ-09: Slots are written to the WAV as channels in the order they were specified
- REQ-10: Unspecified slots are silently discarded
- REQ-11: If a specified slot is missing from a frame, silence (zero) is written for that channel

### WAV Writing

- REQ-12: HLA opens the output WAV file when the first frame is received
- REQ-13: WAV is written as standard PCM (integer samples) at the sample rate derived from frame timing
- REQ-14: WAV header is refreshed periodically (every ~1000 frames) so partial captures are playable
- REQ-15: Each frame in the WAV corresponds to one TDM frame (one sample per channel per frame)

### Error Handling

- REQ-16: If `output_path` is empty or unset, HLA surfaces a clear error via its frame output
- REQ-17: If `slots` is invalid (unparseable), HLA surfaces a clear error
- REQ-18: Error frames from the LLA (short slot, extra slot, bitclock error) are written as silence for that sample rather than crashing
- REQ-19: HLA gracefully handles frames with severity = error — logs them, does not raise

### Documentation

- REQ-20: README section explains how to install the HLA in Logic 2
- REQ-21: README section explains the settings fields (slots, output_path, bit_depth) with examples
- REQ-22: README notes the requirement for absolute paths in `output_path`

## Out of Scope (v1.5)

- RF64 support for WAV files exceeding 4 GiB
- Multiple simultaneous output files
- Float32 sample format
- Marketplace/signed extension packaging
- HLA-driven waveform display annotations

## Success Criteria

- HLA loads in Logic 2 without errors
- A TDM capture with 8 slots can produce a 2-channel WAV from slots 0 and 2
- The WAV file is readable in a standard audio player (Audacity, etc.)
- Partial captures (Logic 2 stopped mid-capture) produce a valid playable WAV

---
*Created: 2026-03-02 for v1.5 milestone*
