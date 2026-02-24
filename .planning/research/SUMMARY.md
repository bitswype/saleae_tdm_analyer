# Project Research Summary

**Project:** Saleae TDM Analyzer — Audit and Code Quality Improvement
**Domain:** Saleae Logic 2 C++ Protocol Analyzer Plugin (TDM/I2S audio)
**Researched:** 2026-02-23
**Confidence:** HIGH

## Executive Summary

This is not a greenfield project. The TDM analyzer plugin is feature-complete and already substantially more capable than the official Saleae I2S reference analyzer. The project runs as a C++ shared library loaded by Logic 2, implementing the `Analyzer2` SDK interface to decode TDM audio frames (I2S, Left-Justified, Right-Justified, and DSP Mode A/B variants) with up to 256 slots per frame. The current codebase has all the right architectural pieces in place — FrameV2 support, WAV export, advanced error detection, simulation data generator, and cross-platform CI/CD. The goal of any upcoming work is to raise correctness and code quality confidence, not to add features.

The recommended approach is a structured audit that fixes a set of well-identified, concrete bugs before adding any new functionality. Research confirmed nine specific pitfalls in the current codebase, three of which are critical correctness defects: a fixed-size sprintf buffer that will overflow if all four error flags fire simultaneously, a constructor bug that initializes the export-type UI control with the wrong variable, and WAV header structs that rely on a GCC-only pragma without any compile-time verification. The WAV export also has a channel-alignment correctness bug that silently corrupts multi-channel output whenever a SHORT_SLOT error occurs in a captured frame.

The key external constraint is that Logic 2's `AddExportOption()` / `AddExportExtension()` API is permanently non-functional — this is a confirmed, unresolved SDK limitation that has been open since approximately 2021. The current settings-dropdown workaround for routing WAV vs CSV export is the correct permanent solution. No further investigation of this issue is needed. The SDK itself has been stable since July 2023 with no API changes, and the project's GitHub Actions CI is already using the current v4 action versions. The only build hygiene fix needed is pinning the SDK FetchContent to a specific commit hash rather than `master`.

## Key Findings

### Recommended Stack

The stack is fully determined by the Saleae ecosystem — there are no alternatives. The plugin is a C++ shared library built against the Saleae AnalyzerSDK (last updated July 2023, commit `114a3b8`), compiled with C++11 using CMake 3.13+. The SDK is fetched via FetchContent at configure time. No third-party libraries are appropriate; the plugin runs inside the Logic 2 process.

**Core technologies:**
- **Saleae AnalyzerSDK (master @ `114a3b8`)**: Only SDK supported by Logic 2; stable since July 2023 with no pending API changes
- **C++11**: SDK mandates this standard; going newer risks ABI compatibility issues with the precompiled SDK library
- **CMake 3.13+**: Required by the FetchContent-based SDK acquisition pattern; already in use
- **FrameV2 API (built into SDK)**: Provides named-column data table display and HLA chaining; requires Logic 2.3.43+; already implemented in TDM analyzer
- **GitHub Actions (ubuntu/windows/macos, v4)**: CI already current; all three platforms tested

One critical build hygiene issue: `ExternalAnalyzerSDK.cmake` uses `GIT_TAG master` which is non-reproducible. Should be pinned to `114a3b8306e6a5008453546eda003db15b002027`.

See `.planning/research/STACK.md` for full detail.

### Expected Features

This is an audit milestone, not a launch. All table-stakes and differentiating features are already implemented. Research confirmed the feature set is complete and compares favorably against the official Saleae I2S analyzer in every dimension.

**Already shipped (v1 — current state):**
- Core TDM decoding (I2S, LJ, RJ, DSP variants) with configurable slot/data widths
- FrameV2 with named fields ("channel", "data", "errors", "warnings", "frame #")
- CSV export with timing, flags, and signed/unsigned values
- WAV export via TXT/CSV workaround (confirmed permanent; SDK bug will not be fixed)
- Advanced error detection (bitclock, missed data, missed frame sync, short/extra slot)
- Multi-slot support (1-256 slots/frame)
- Simulation data generator (sine wave, counter, static patterns)
- Cross-platform CI/CD for Windows, macOS, Linux

**Should add after audit (v1.x):**
- Sample rate sanity check / warning
- Named preset configurations (I2S, RJ, LJ, DSP)

**Confirmed out of scope permanently:**
- Custom export types via `AddExportOption()` — blocked by Logic 2 SDK bug; Saleae intends to further pare back C++ export API, not restore it
- Real-time audio streaming — architecturally impossible in the plugin sandbox
- Multi-data-channel support within one analyzer instance — requires SDK architectural changes

See `.planning/research/FEATURES.md` for full detail.

### Architecture Approach

The plugin follows the standard four-component Saleae SDK architecture: `TdmAnalyzer` (core analysis loop), `TdmAnalyzerSettings` (UI and serialization), `TdmAnalyzerResults` (display and export), and `TdmSimulationDataGenerator` (testing). The architecture is sound and already matches Saleae's own reference analyzers. The critical patterns — `SetupResults()` for initialization, dual Frame+FrameV2 submission, `ClearTabularText()` before `AddTabularText()`, `UpdateExportProgressAndCheckForCancel()` in export loops, and `CheckIfThreadShouldExit()` in the worker thread — are all correctly implemented.

**Major components:**
1. **TdmAnalyzer (Analyzer2 subclass)** — Core analysis loop in `WorkerThread()`; reads clock/frame/data edges, accumulates bits into slots, dispatches to `AnalyzeTdmSlot()`
2. **TdmAnalyzerSettings (AnalyzerSettings subclass)** — 12 configurable parameters including slots/frame, bits/slot, data bits, alignment, signed mode, advanced analysis, export file type; settings persist via SimpleArchive
3. **TdmAnalyzerResults (AnalyzerResults subclass)** — Bubble text, data table, CSV and WAV export; WAV export uses `PCMWaveFileHandler` with `writeLittleEndianData()` for portable binary output
4. **TdmSimulationDataGenerator** — Generates sine wave, counter, and static TDM frames for hardware-free testing

The export anti-pattern of routing format via `export_type_user_id` has correctly been avoided — format selection goes through `mExportFileType` in settings. This is the correct permanent workaround.

See `.planning/research/ARCHITECTURE.md` for full detail.

### Critical Pitfalls

All nine identified pitfalls are localized to the current codebase and addressable in a single audit pass. Three are critical correctness defects; six are moderate quality/robustness issues.

1. **sprintf buffer overflow in error string formatting** — Fixed-size `char error_str[80]` with chained `sprintf` appends; all four simultaneous error flags fit today but any change overflows silently. Fix: replace all `sprintf` with `snprintf(buf + used, sizeof(buf) - used, ...)` and remove the `#pragma warning(disable: 4996)` suppressions. Affects `TdmAnalyzer.cpp` lines 311-329 and `TdmAnalyzerResults.cpp` lines 51-71 and 490-510.

2. **Wrong variable in settings constructor** — `mExportFileTypeInterface->SetNumber(mEnableAdvancedAnalysis)` at `TdmAnalyzerSettings.cpp` line 136 should use `mExportFileType`. Masked by both defaulting to 0, but brittle. Fix: one-line correction; verify `UpdateInterfacesFromSettings()` consistency.

3. **`#pragma scalar_storage_order` is GCC-only** — WAV header structs use this pragma without any `static_assert` on struct size; Clang and MSVC silently ignore it. Fix: add `static_assert(sizeof(WavePCMHeader) == 44)` and `static_assert(sizeof(WavePCMExtendedHeader) == 80)`.

4. **SHORT_SLOT causes WAV channel alignment drift** — When a frame is truncated mid-slot, `GenerateWAV()` skips that slot's `addSample()` call, causing all subsequent channels to shift by one position. Fix: call `addSample(0)` as a zero-fill placeholder for SHORT_SLOT frames to preserve channel alignment.

5. **Settings format has no version field** — `LoadSettings()` has no numeric version after the magic string; adding or reordering any field in the future will silently corrupt loaded projects. Fix: add `U32 version` field immediately after the magic string; gate new fields on version check.

See `.planning/research/PITFALLS.md` for all nine pitfalls with full detail and recovery strategies.

## Implications for Roadmap

The research points to a single, focused audit phase rather than a multi-phase feature development roadmap. All features are built. The work is correctness, robustness, and build hygiene.

### Phase 1: Core Correctness Fixes

**Rationale:** Three critical pitfalls (buffer overflow, wrong constructor variable, WAV channel alignment) are correctness bugs that produce incorrect or potentially undefined behavior. They must be resolved before any other work — they represent latent defects that could corrupt output silently. The buffer overflow is worsened by actively suppressed compiler warnings, which must be removed.

**Delivers:** A codebase free of known correctness defects, with compiler warnings restored and heeded.

**Addresses:** sprintf safety (Pitfall 1), constructor variable mismatch (Pitfall 2), WAV channel alignment drift (Pitfall 6)

**Key actions:**
- Replace all `sprintf` with `snprintf` at all three call sites
- Remove `#pragma warning(disable: 4996)` suppressions from `TdmAnalyzerResults.cpp` and `TdmAnalyzerSettings.cpp`
- Fix constructor to use `mExportFileType` instead of `mEnableAdvancedAnalysis`
- Fix `GenerateWAV()` to zero-fill missing slots for SHORT_SLOT frames

### Phase 2: Build Hygiene and Portability Guards

**Rationale:** Portability and reproducibility issues are lower urgency than correctness bugs but should be addressed before any new development to ensure a stable foundation. Pinning the SDK prevents future build surprises; `static_assert` guards catch struct packing issues silently introduced by any compiler that ignores the GCC-only pragma.

**Delivers:** Reproducible builds, compile-time verified WAV struct layout, and a foundation safe to build new features on.

**Addresses:** `scalar_storage_order` portability (Pitfall 3), `GIT_TAG master` instability (Pitfall 7)

**Key actions:**
- Pin `ExternalAnalyzerSDK.cmake` `GIT_TAG` to `114a3b8306e6a5008453546eda003db15b002027`
- Add `static_assert(sizeof(WavePCMHeader) == 44)` and `static_assert(sizeof(WavePCMExtendedHeader) == 80)`
- Document the pinned SDK version and the scalar_storage_order pragma limitation in comments

### Phase 3: Code Quality and Future-Proofing

**Rationale:** The remaining pitfalls are quality and maintainability issues: settings versioning prevents future field additions from silently corrupting saved projects; the misleading enum name causes confusion for maintainers; the WAV 4GB limit and header seek frequency are edge-case correctness and performance issues. These are lower urgency but should be addressed to leave the codebase in a state that can be extended safely.

**Delivers:** A codebase that is safe to extend with new settings fields, with correct domain terminology, and with documented known limits.

**Addresses:** WAV 4GB overflow (Pitfall 4), WAV header seek frequency (Pitfall 5), settings version missing (Pitfall 8), misleading enum names (Pitfall 9)

**Key actions:**
- Add `U32 version` field to settings archive; gate field loading on version
- Add pre-export size check in `GenerateWAV()` to detect and warn on >4GB captures
- Increase WAV header update interval from every 10ms to every 1000ms
- Rename `BITS_SHIFTED_RIGHT_1` / `NO_SHIFT` to `DSP_MODE_A` / `DSP_MODE_B` with explanatory comments

### Phase Ordering Rationale

- Phase 1 before Phase 2 because correctness bugs should not be deferred behind hygiene work; a buffer overflow is more urgent than a pragma comment
- Phase 2 before Phase 3 because the `static_assert` guards and SDK pin provide a stable platform for the quality pass
- Phase 3 is independently safe to reorder, but settings versioning should happen before any new settings fields are introduced — so it has a soft deadline tied to any feature additions
- All three phases are audit work on existing code, not greenfield development — they can move quickly

### Research Flags

Phases with standard patterns (no additional research needed):
- **Phase 1:** All fixes are well-understood one-to-several-line changes with clear correct implementations documented in PITFALLS.md
- **Phase 2:** CMake FetchContent pinning and `static_assert` are standard practices; no ambiguity
- **Phase 3:** Settings versioning pattern documented in PITFALLS.md; enum renaming is mechanical

No phase requires `/gsd:research-phase` during planning. All issues have been researched to resolution. The only open question (Logic 2 custom export bug) is confirmed permanent and requires no further investigation.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | SDK verified via `git ls-remote`; Logic 2 version confirmed via official changelog; all API claims verified against SDK headers |
| Features | HIGH | Feature status confirmed against official I2S analyzer source and Saleae changelog; export bug confirmed via ideas forum and Saleae staff posts |
| Architecture | HIGH | Patterns derived from official Saleae analyzer sources (serial-analyzer, i2c-analyzer); anti-patterns verified against confirmed SDK behavior |
| Pitfalls | HIGH | All pitfalls grounded in direct source code inspection of the actual codebase at specific line numbers; external claims (WAV spec, LLVM issue tracker, GCC docs) verified |

**Overall confidence:** HIGH

### Gaps to Address

- **WAV 4GB limit — RF64 alternative:** Research confirms RF64/BW64 as the correct remedy for >4GB WAV files, but implementation complexity was not fully scoped. For typical embedded audio captures this limit is unlikely to be hit; the pre-export warning check is sufficient for the audit phase. Full RF64 implementation is a v2+ consideration.
- **`LoadSettings`/`SaveSettings` vs. Logic 2 auto-serialization:** The SDK documentation claims Logic 2 auto-serializes settings interfaces, but this is not confirmed to replace manual `SimpleArchive` serialization for all cases. The current approach (manual `SimpleArchive`) is confirmed working in practice across multiple third-party analyzers; maintain it until Saleae clarifies the auto-serialization scope.
- **8-bit WAV unsigned encoding:** PITFALLS.md source material notes 8-bit PCM WAV must use unsigned offset binary (not signed two's complement). The current codebase support for 8-bit depth should be verified against this spec requirement during the Phase 1 correctness pass.

## Sources

### Primary (HIGH confidence)
- GitHub remote inspection of `saleae/AnalyzerSDK` — SDK HEAD commit, branch list, last change date
- `https://github.com/saleae/SampleAnalyzer` — reference implementation patterns, recent changes (auto_ptr removal, Actions v4)
- `https://github.com/saleae/serial-analyzer` and `i2s-analyzer` — official reference implementations for architectural patterns
- `https://ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/` — custom export bug status (Open, no fix planned)
- `https://discuss.saleae.com/t/export-formats-for-low-level-analyzers/1040` — Saleae staff confirmation export not supported in Logic 2
- `https://ideas.saleae.com/f/changelog/` — Logic 2 version history through 2.4.40 (December 2025)
- `https://support.saleae.com/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk` — FrameV2 requirements and implementation
- `https://github.com/llvm/llvm-project/issues/34641` — confirmation Clang does not implement `scalar_storage_order`
- `http://soundfile.sapp.org/doc/WaveFormat/` — WAV PCM format specification (32-bit chunk size limit)
- Direct source code inspection of `/src/` at specific line numbers — all pitfall line references

### Secondary (MEDIUM confidence)
- `https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md` — notes on LoadSettings/SaveSettings and Packet/Transaction deprecation (content obtained via search result excerpts, not direct fetch)
- `https://discuss.saleae.com/t/missing-v1-analyzer-sdk-plugin-features-and-ideas/684` — Saleae intent to further pare back C++ SDK export capability

---
*Research completed: 2026-02-23*
*Ready for roadmap: yes*
