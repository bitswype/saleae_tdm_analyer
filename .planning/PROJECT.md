# TDM Analyzer for Saleae Logic 2

## What This Is

A protocol analyzer plugin for Saleae Logic 2 that decodes TDM (Time-Division Multiplexing) audio streams. It supports common TDM variants including I2S, Left-Justified, Right-Justified, and DSP modes, with configurable slot sizes, data alignment, and bit ordering. It can export decoded data as CSV or WAV files.

## Core Value

Correctly decode TDM audio data from logic analyzer captures with confidence that the results are accurate and the code is trustworthy.

## Requirements

### Validated

- ✓ Decode TDM frames with 1-256 slots per frame — existing
- ✓ Support slot sizes from 2-64 bits with configurable data bits — existing
- ✓ Configurable frame sync polarity (rising/falling) and timing (first/last bit) — existing
- ✓ Configurable bit clock edge (rising/falling) for data latching — existing
- ✓ Left and right justified data alignment — existing
- ✓ MSB-first and LSB-first bit ordering — existing
- ✓ Export decoded data as CSV with timing, flags, and slot data — existing
- ✓ Export decoded data as WAV file (workaround via TXT/CSV export due to Logic 2 bug) — existing
- ✓ WAV export with proper PCM headers, multi-channel support, and periodic header updates — existing
- ✓ Detect and flag short slots (truncated frames) — existing
- ✓ Detect and flag extra/unexpected slots — existing
- ✓ Advanced analysis: bitclock error detection — existing
- ✓ Advanced analysis: missed data detection (data transitions between clock edges) — existing
- ✓ Advanced analysis: missed frame sync detection — existing
- ✓ Simulation data generator for testing without hardware — existing
- ✓ Cross-platform builds (Windows, macOS x86_64/ARM64, Linux) — existing
- ✓ CI/CD via GitHub Actions — existing

### Active

- [ ] Full code audit — correctness, performance, and code quality review
- [ ] SDK API update check — verify no breaking changes or new capabilities since v2.3.58
- [ ] Logic 2 custom export type bug investigation — determine if WAV export workaround is still needed
- [ ] Build process documentation — clean up README build instructions for clarity

### Out of Scope

- Sample rate sanity check in settings dialog — future milestone, non-blocking warning when rate is insufficient
- Named standard presets (I2S, LJ, RJ, DSP Mode A/B) — future milestone, auto-fill settings from protocol name
- Settings dialog improvements — future milestone, scope TBD
- New TDM format support — current options believed to cover all known I2S/TDM configurations

## Context

- Plugin built against the Saleae AnalyzerSDK (fetched via CMake FetchContent from GitHub)
- C++11 codebase, ~4 source files plus simulation generator
- WAV export currently hijacks the TXT/CSV export callback because Logic 2 doesn't display custom export types in its UI (documented bug as of v2.3.58)
- Build system is CMake-based with cross-platform support, but the build steps and what they do aren't well explained in the README
- Codebase map exists at `.planning/codebase/` with full architecture, stack, and conventions analysis
- User wants confidence in correctness and the ability to understand and modify the code themselves

## Constraints

- **SDK compatibility**: Must remain compatible with the Saleae AnalyzerSDK (Analyzer2 base class interface)
- **C++11**: Locked to C++11 standard per CMake configuration and SDK requirements
- **Cross-platform**: Changes must build on Windows (MSVC), macOS (x86_64 + ARM64), and Linux (GCC)
- **Plugin interface**: Entry points (`CreateAnalyzer`, `DestroyAnalyzer`, `GetAnalyzerName`) are fixed by SDK contract

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Audit before new features | Establish confidence in existing code before adding complexity | — Pending |
| WAV export via TXT/CSV hijack | Logic 2 bug prevents custom export types from rendering | ⚠️ Revisit — check if bug is fixed |
| Focus on docs over features this milestone | User needs to understand the build and codebase before extending it | — Pending |

---
*Last updated: 2026-02-23 after initialization*
