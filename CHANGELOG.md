# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Python HLA: TDM Audio Stream** — a Logic 2 High Level Analyzer (`hla-audio-stream/TdmAudioStream.py`) that streams selected TDM slots as live PCM audio over a TCP socket, with configurable ring buffer, 16 or 32-bit depth, and auto-derived sample rate
- **Companion CLI: tdm-audio-bridge** — connects to the Audio Stream HLA's TCP server and plays decoded audio through local audio devices or virtual sound cards (VB-CABLE, BlackHole, PipeWire null-sink), with auto-reconnect
- **Test harness: tdm-test-harness** — standalone testing tool that drives the HLA without Logic 2 or hardware; supports automated verification with structured JSON output, test signal generation (sine, silence, ramp, WAV), and agent-friendly exit codes

## [2.2.0] - 2026-03-02

### Added

- **Python HLA: TDM WAV Export** — a Logic 2 High Level Analyzer (`hla/TdmWavExport.py`) that exports selected TDM slots to a WAV file in real time during capture, with slot selection via comma/range syntax, 16 or 32-bit depth, and auto-derived sample rate from frame timing

## [2.1.0] - 2026-02-25

### Changed

- **FrameV2 schema:** Replaced `channel` (integer) field with `slot` (integer, same 0-based value) [FRM2-06]
- **FrameV2 schema:** Replaced `errors` and `warnings` string fields with `severity` string field (`error`/`warning`/`ok`) [FRM2-07]
- **Settings validation:** Zero-parameter guards reject frame_rate=0, slots_per_frame=0, or bits_per_slot=0 before analysis [SRAT-02]
- **Settings validation:** Configurations requiring bit clock >500 MHz are rejected with detailed error message showing the math [SRAT-02]
- **FrameV2 schema:** Slot severity elevated from "ok" to "warning" when capture sample rate is below 4x bit clock and no decode errors present [SRAT-01]

### Added

- **RF64 WAV export for captures exceeding 4 GiB** — produces valid RF64 files (EBU TECH 3306) that open in Audacity, FFmpeg, and other standard tools [RF64-03]
- **FrameV2 schema:** Added boolean fields `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, `missed_frame_sync` to every decoded slot row [FRM2-01 through FRM2-05]
- **FrameV2 schema:** Added boolean `low_sample_rate` field to every decoded slot row — true when capture rate < 4x bit clock [SRAT-01]
- **FrameV2 advisory:** New "advisory" frame type emitted as row 0 when sample rate is below 4x bit clock, with human-readable message showing the math [SRAT-01]

### Removed

- **4 GiB text-error guard in WAV export** — replaced by conditional RF64 path; exports that exceed 4 GiB now produce a valid RF64 file instead of a text message explaining the abort [RF64-04]

### Migration

```python
# Before (v2.0.0)
channel = frame.data["channel"]
errors = frame.data["errors"]    # string like "E: Short Slot E: Data Error "
warnings = frame.data["warnings"]  # string like "W: Extra Slot "

# After (v2.1.0+)
slot = frame.data["slot"]                       # same 0-based integer value
severity = frame.data["severity"]               # "error", "warning", or "ok"
is_short = frame.data["short_slot"]             # True or False
is_extra = frame.data["extra_slot"]             # True or False
is_bitclock_err = frame.data["bitclock_error"]  # True or False
is_missed_data = frame.data["missed_data"]      # True or False
is_missed_sync = frame.data["missed_frame_sync"]  # True or False
low_rate = frame.data["low_sample_rate"]        # True or False
# Advisory frames have type "advisory" (not "slot")
```

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

[Unreleased]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/bitswype/saleae_tdm_analyer/releases/tag/v2.0.0
