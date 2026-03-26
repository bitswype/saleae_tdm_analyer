# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.4.0] - 2026-03-25

### Added

- **Performance tuning settings** — two new analyzer settings: "Data Table / HLA Output" (Full/Minimal/Off) and "Waveform Markers" (All/Slot/None). Minimal+Slot is 1.8x faster than the default Full+All; Off+None is 3.1x faster. These are user-configurable in the analyzer settings panel.
- **Cython fast decode for Audio Stream HLA** — moves the entire decode() hot path to C via Cython, with a four-tier fallback chain (Cython > raw C > cffi > pure Python). 4-7x faster than pure Python.
- **Batch PCM packing in Audio Stream HLA** — accumulates up to 1024 frames per TCP send, reducing GIL contention from 44K+ individual sends/sec to ~43 batched sends/sec.
- **C++ correctness test suite** — 58 tests across 10 categories verifying decode accuracy, sign conversion, error detection, boundary values, and FrameV2 field correctness.
- **C++ performance benchmark** — 16-configuration throughput benchmark with optional per-function profiling instrumentation (`ENABLE_PROFILING`).
- **Real SDK benchmark timing** — compile-time flag (`ENABLE_BENCHMARK_TIMING`) embeds self-timing in the DLL that writes decode duration to `%USERPROFILE%\tdm_benchmark_timing.json`. Zero overhead when compiled out.
- **Logic 2 automation benchmark tooling** — `tools/benchmark_logic2.py` for API-driven benchmarks, `tools/prepare_benchmark_captures.py` for generating .sal files with UI display disabled via meta.json patching.
- **Python HLA: TDM Audio Stream** — a Logic 2 High Level Analyzer that streams selected TDM slots as live PCM audio over a TCP socket, with configurable ring buffer, 16 or 32-bit depth, and auto-derived sample rate.
- **Companion CLI: tdm-audio-bridge** — connects to the Audio Stream HLA's TCP server and plays decoded audio through local audio devices or virtual sound cards, with auto-reconnect and tkinter GUI.
- **Test harness: tdm-test-harness** — standalone testing tool that drives the HLA without Logic 2 or hardware; supports automated verification, quality sweep, and signal generation.
- **64-test Python HLA test suite** — covers all decode paths including C-port-specific edge cases for all four backends.

### Fixed

- **Critical: FrameV2 member reuse OOM crash** — the SDK's Add* methods append (not overwrite) and provide no Clear/Reset. Reusing a FrameV2 across frames caused O(N^2) memory growth, consuming 28 GB of RAM on a <1 second 8-channel capture. Reverted to local FrameV2 per frame.

### Changed

- **HLA decode performance** — inlined sign conversion with pre-computed masks, batch PCM packing via struct.pack_into, sender loop batching (1024 frames per sendall).
- **Documentation** — comprehensive performance narrative (tests/PERFORMANCE.md), profiling results, optimization results, C extension design rationale, test architecture documentation.

## [2.2.0] - 2026-03-02

### Added

- **Python HLA: TDM WAV Export** — a Logic 2 High Level Analyzer (`hla-wav-export/TdmWavExport.py`) that exports selected TDM slots to a WAV file in real time during capture, with slot selection via comma/range syntax, 16 or 32-bit depth, and auto-derived sample rate from frame timing

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

[Unreleased]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.4.0...HEAD
[2.4.0]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.3.0...v2.4.0
[2.2.0]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/bitswype/saleae_tdm_analyer/releases/tag/v2.0.0
