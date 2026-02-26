# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **FrameV2 schema:** Replaced `channel` (integer) field with `slot` (integer, same 0-based value) [FRM2-06]
- **FrameV2 schema:** Replaced `errors` and `warnings` string fields with `severity` string field (`error`/`warning`/`ok`) [FRM2-07]

### Added

- **FrameV2 schema:** Added boolean fields `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, `missed_frame_sync` to every decoded slot row [FRM2-01 through FRM2-05]

### Migration

```python
# Before (v2.0.0)
channel = frame.data["channel"]
errors = frame.data["errors"]    # string like "E: Short Slot E: Data Error "
warnings = frame.data["warnings"]  # string like "W: Extra Slot "

# After (unreleased)
slot = frame.data["slot"]           # same 0-based integer value
severity = frame.data["severity"]   # "error", "warning", or "ok"
is_short = frame.data["short_slot"] # True or False
is_extra = frame.data["extra_slot"] # True or False
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

[Unreleased]: https://github.com/bitswype/saleae_tdm_analyer/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/bitswype/saleae_tdm_analyer/releases/tag/v2.0.0
