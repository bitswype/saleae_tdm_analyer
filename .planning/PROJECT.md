# TDM Analyzer for Saleae Logic 2

## What This Is

A protocol analyzer plugin for Saleae Logic 2 that decodes TDM (Time-Division Multiplexing) audio streams. It supports common TDM variants including I2S, Left-Justified, Right-Justified, and DSP modes, with configurable slot sizes, data alignment, and bit ordering. It can export decoded data as CSV or WAV files. After v1.0, the codebase is audited for correctness, builds are reproducible, and documentation reflects the permanent architecture.

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
- ✓ Export decoded data as WAV file (via TXT/CSV export path, permanent Saleae architecture) — existing
- ✓ WAV export with proper PCM headers, multi-channel support, and periodic header updates — existing
- ✓ Detect and flag short slots (truncated frames) — existing
- ✓ Detect and flag extra/unexpected slots — existing
- ✓ Advanced analysis: bitclock error detection — existing
- ✓ Advanced analysis: missed data detection (data transitions between clock edges) — existing
- ✓ Advanced analysis: missed frame sync detection — existing
- ✓ Simulation data generator for testing without hardware — existing
- ✓ Cross-platform builds (Windows, macOS x86_64/ARM64, Linux) — existing
- ✓ CI/CD via GitHub Actions — existing
- ✓ Full code audit — correctness bugs fixed, code quality improved — v1.0
- ✓ Build process documentation — README explains FetchContent SDK download and cmake steps — v1.0
- ✓ WAV export architecture documented — TXT/CSV workaround confirmed permanent by Saleae — v1.0
- ✓ Reproducible builds — AnalyzerSDK pinned to full commit SHA — v1.0
- ✓ WAV 4 GiB overflow guard — pre-export size check prevents silent corruption — v1.0

### Active

- [ ] SDK audit and update — check what changed since 114a3b8, update to latest commit, adopt useful new APIs
- [ ] FrameV2 data enrichment — add structured fields to decoded frames for Logic 2 HLA/table display
- [ ] RF64 WAV support — replace 4 GiB guard with proper RF64 headers for large exports
- [ ] Sample rate sanity check — non-blocking warning when Logic analyzer sample rate is insufficient for TDM parameters

## Current Milestone: v1.4 SDK & Export Modernization

**Goal:** Update the AnalyzerSDK to latest, enrich decoded data with FrameV2 structured fields, add RF64 support for large WAV exports, and warn users about insufficient sample rates.

**Target features:**
- SDK audit + update to latest AnalyzerSDK commit
- FrameV2 enrichment for Logic 2 HLA/table display
- RF64 WAV export for captures exceeding 4 GiB
- Sample rate sanity check in settings dialog

### Out of Scope

- Named standard presets (I2S, LJ, RJ, DSP Mode A/B) — future milestone, auto-fill settings from protocol name
- Settings dialog improvements — future milestone, scope TBD
- New TDM format support — current options believed to cover all known I2S/TDM configurations

## Context

- Plugin built against the Saleae AnalyzerSDK (fetched via CMake FetchContent, pinned to commit SHA 114a3b8)
- C++11 codebase, 2,103 LOC across 5 source files plus simulation generator
- WAV export uses the TXT/CSV export callback — Logic 2 does not support custom export types (confirmed permanent Saleae design decision)
- Build system is CMake-based with cross-platform support; README explains what each step does
- All sprintf calls replaced with snprintf; WAV channel alignment preserved on error frames
- Enum values use protocol-semantic names (DSP_MODE_A/DSP_MODE_B)
- WAV export has a 4 GiB pre-export overflow guard
- Codebase map exists at `.planning/codebase/` with full architecture, stack, and conventions analysis
- v1.0 shipped 2026-02-25 — correctness, build hygiene, and documentation audit complete

## Constraints

- **SDK compatibility**: Must remain compatible with the Saleae AnalyzerSDK (Analyzer2 base class interface)
- **C++11**: Locked to C++11 standard per CMake configuration and SDK requirements
- **Cross-platform**: Changes must build on Windows (MSVC), macOS (x86_64 + ARM64), and Linux (GCC)
- **Plugin interface**: Entry points (`CreateAnalyzer`, `DestroyAnalyzer`, `GetAnalyzerName`) are fixed by SDK contract

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Audit before new features | Establish confidence in existing code before adding complexity | ✓ Good — 4 correctness bugs found and fixed |
| WAV export via TXT/CSV path | Logic 2 does not support custom export types — confirmed permanent Saleae design decision | ✓ Good — documented as permanent architecture |
| Focus on docs over features this milestone | User needs to understand the build and codebase before extending it | ✓ Good — README rewritten with explanatory prose |
| snprintf with offset tracking over std::string | Minimal surgical fix preserving existing code structure | ✓ Good — all sprintf replaced safely |
| Pin SDK to full commit SHA | Reproducible builds that don't break on upstream changes | ✓ Good — 114a3b8 pinned |
| static_assert after pragma pack(pop) | Catches Clang/MSVC packing errors that GCC-only scalar_storage_order misses | ✓ Good — compile-time safety |
| DSP_MODE_A=0, DSP_MODE_B=1 order preserved | Serialized settings files (SimpleArchive integers) remain backward compatible | ✓ Good — no migration needed |
| WAV size guard writes plain text to .wav path | User sees warning regardless of how they open the file | ✓ Good — prevents silent corruption |

---
*Last updated: 2026-02-25 after v1.4 milestone start*
