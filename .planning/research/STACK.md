# Stack Research

**Domain:** Saleae Logic 2 Low-Level Protocol Analyzer Plugin (C++ SDK)
**Researched:** 2026-02-23
**Confidence:** HIGH (SDK state verified via GitHub remote inspection; Logic 2 version confirmed via official forums/changelog)

---

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| Saleae AnalyzerSDK | master @ `114a3b8` (Jul 7, 2023) | Plugin base classes, channel data interfaces | Only SDK supported by Logic 2; no alternative |
| C++ | C++11 | Plugin implementation language | SDK mandates C++11; going newer risks compatibility with the precompiled SDK library |
| CMake | 3.13+ | Build system | Required by SDK's FetchContent setup; 3.13 adds `FetchContent_MakeAvailable` convenience; SampleAnalyzer requires 3.13 |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| FrameV2 API (built into SDK) | Available since Logic 2.3.43 | Structured per-frame data visible in Logic 2 data table and consumable by High Level Analyzers (HLAs) | Use when you want decoded slot data to appear as named columns in the analyzer results table, or to feed HLA chains. NOT required for basic decode. |
| Standard C++ library only | C++11 stdlib | File I/O, string ops, math | No third-party libraries are needed or appropriate for an SDK plugin; plugin runs inside the Logic 2 process |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| GCC 14 (Linux), MSVC 2017+ (Windows), Xcode (macOS) | Compilation | CI uses gcc-14; SDK requires 64-bit targets only (SDK CMake config fatally errors on 32-bit) |
| GitHub Actions (ubuntu-latest / windows-latest / macos-latest) | CI/CD | Current workflow uses actions/checkout@v4, upload-artifact@v4 — these are current as of late 2024 |
| Clang Format | Code style enforcement | `.clang-format` present in repo; Allman braces, 4-space indent, 140-char line limit |

---

## SDK State: What Has and Has Not Changed

### SDK Repository (github.com/saleae/AnalyzerSDK)

**Last commit:** July 7, 2023 (`114a3b8`) — added macOS ARM64 support via `lib_arm64/` and `CMAKE_OSX_ARCHITECTURES`-aware CMake config.

**Tags:** Only one tag exists: `alpha-1`. There is no `v1.0`, `v2.x`, or other stable release tag. The convention is to use `master`.

**Branches of interest:**
- `master` — current stable target
- `1.1.14-legacy` — Logic 1.x legacy branch, do not use for Logic 2
- `feature/framev2-flat`, `feature/framev2-marker`, `feature/new-frame` — experimental, not merged

**Conclusion:** The SDK has been stable (no API changes) since July 2023. Core `Analyzer2` interface has not changed since at least early 2022. No breaking changes are pending.

### SampleAnalyzer Repository (github.com/saleae/SampleAnalyzer)

**Last commit:** October 28, 2024 — updated `Analyzer_API.md` documentation.

**Notable 2024 changes:**
- **August 2024:** Removed `std::auto_ptr` usage throughout SampleAnalyzer (PRs #33, #34). `std::auto_ptr` is deprecated in C++11 and removed in C++17. While the TDM analyzer's CMakeLists.txt locks to C++11 (where `auto_ptr` still compiles), adopting `std::unique_ptr` is the current best practice per SampleAnalyzer.
- **October 2024:** GitHub Actions updated to v4 (`actions/checkout@v4`, `actions/upload-artifact@v4`, `actions/download-artifact@v4`). The TDM analyzer's `build.yml` already uses these v4 actions — it is current.
- **Documentation note added:** `Analyzer_API.md` now explicitly states it "has not been updated since the original release" and that `LoadSettings`/`SaveSettings` are no longer used, and Packet/Transaction abstraction was not implemented in favor of FrameV2 and HLAs.

### Logic 2 Application

**Current version:** 2.4.40 (December 17, 2025)

**Recent release focus:** Hardware support (Logic MSO), bug fixes, UI polish. No SDK-level changes in any 2.4.x release.

---

## Critical: Custom Export Types Still Broken in Logic 2

**Status as of Logic 2.4.40:** CONFIRMED NOT FIXED. Open since ~2021.

The `AddExportOption()` and `AddExportExtension()` methods of `AnalyzerSettings` exist in the SDK but are **not honored by Logic 2**. Custom export menu entries defined by a plugin do not appear in the Logic 2 UI. This is a documented, acknowledged, unfixed limitation — not a user error.

**Feature request:** https://ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/ (13 voters, still Open as of research date)

**Community discussion:** https://discuss.saleae.com/t/export-formats-for-low-level-analyzers/1040 — Saleae confirmed "we haven't added support for custom export options in Logic 2."

**Implication for TDM analyzer:** The WAV export workaround (hijacking the TXT/CSV export callback, controlled by an analyzer setting that selects the export format) is still correct and still necessary. There is no better option available. The PROJECT.md item "Logic 2 custom export type bug investigation" can be closed: the bug is confirmed still present in Logic 2.4.40.

---

## GIT_TAG Strategy: `master` vs Pinned Commit

**Current state:** `ExternalAnalyzerSDK.cmake` uses `GIT_TAG master` — always fetches the latest master.

**Risk:** Low in practice (SDK is stable, last changed July 2023), but using `master` means builds are theoretically non-reproducible. If Saleae ever commits a breaking change to master, all builds instantly break.

**Recommendation:** Pin to a specific commit hash for reproducibility. Current HEAD is `114a3b8306e6a5008453546eda003db15b002027`.

```cmake
FetchContent_Declare(
    analyzersdk
    GIT_REPOSITORY https://github.com/saleae/AnalyzerSDK.git
    GIT_TAG        114a3b8306e6a5008453546eda003db15b002027  # master as of 2023-07-07
    GIT_SHALLOW    True
    GIT_PROGRESS   True
)
```

**Confidence:** MEDIUM — CMake docs recommend commit hashes for reproducibility; Saleae has no official guidance on pinning vs. master.

---

## FrameV2 API: Optional Enhancement

**What it is:** An opt-in addition to the existing Frame API that produces structured key-value data per frame, visible as named columns in the Logic 2 data table and consumable by High Level Analyzers (HLAs).

**How to enable:**
1. Call `UseFrameV2()` in the analyzer constructor (after `SetAnalyzerSettings()`)
2. Create `FrameV2` objects alongside existing `Frame` objects in `WorkerThread()`
3. Populate with typed fields: `AddString()`, `AddInteger()`, `AddDouble()`, `AddBoolean()`, `AddByte()`, `AddByteArray()`
4. Call `mResults->AddFrameV2(frame_v2, "frame_type_name", start_sample, end_sample)`

**Requirement:** Logic 2 version 2.3.43+. Analyzers that call `UseFrameV2()` **cannot be loaded in older Logic 2 versions**.

**Status for TDM analyzer:** Not currently implemented. Implementing FrameV2 would allow per-slot data (slot number, value, flags) to appear as named columns in the Logic 2 table, rather than relying on the bubble display only. This is an optional enhancement — the analyzer works without it.

---

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| Saleae AnalyzerSDK (C++) LLA | Python High Level Analyzer (HLA) | When building on top of an existing decoded protocol (e.g., adding TDM semantics on top of a raw serial analyzer). Not applicable here — TDM needs raw bit-clock access. |
| CMake + FetchContent | Git submodule for SDK | Submodule was the old approach (pre-CMake FetchContent). FetchContent is the current Saleae-recommended approach per SampleAnalyzer. |
| C++11 | C++14/17/20 | Only if SDK is eventually updated and all target platforms support newer standard. Currently the precompiled SDK library is built against C++11 ABI assumptions. |

---

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| `std::auto_ptr` | Deprecated in C++11, removed in C++17. SampleAnalyzer removed it in August 2024. Will cause compiler warnings/errors. | `std::unique_ptr` |
| `Analyzer` base class (Logic 1) | Logic 2 requires `Analyzer2`. Using `Analyzer` will not load. | `Analyzer2` |
| `LoadSettings()` / `SaveSettings()` | Not used in Logic 2 (confirmed in Analyzer_API.md update). | Settings serialization handled differently by Logic 2 runtime. |
| Packet/Transaction abstraction (`AddPacketToTransaction`, etc.) | Not implemented in Logic 2. Functions exist in SDK but do nothing useful. | FrameV2 + HLAs for higher-level structure, or just flat Frames. |
| `GIT_TAG master` in production | Non-reproducible builds; any Saleae push could silently change behavior | Pin to specific commit hash |

---

## Version Compatibility

| Component | Compatible With | Notes |
|-----------|-----------------|-------|
| AnalyzerSDK master (`114a3b8`) | Logic 2.3.43+ | FrameV2 API requires 2.3.43+; basic Analyzer2 works with any Logic 2 version |
| AnalyzerSDK with `UseFrameV2()` | Logic 2.3.43+ ONLY | Analyzers calling `UseFrameV2()` will fail to load in older Logic 2 |
| C++11 | MSVC 2015+, GCC 4.8+, Clang 3.3+ | SDK CMake config requires 64-bit; 32-bit is fatally rejected |
| CMake 3.13+ | All current platforms | `FetchContent_MakeAvailable` added in 3.14; project uses `FetchContent_Populate` (3.11+) |
| GitHub Actions v4 | Current (2024+) | `actions/checkout@v4`, `upload-artifact@v4`, `download-artifact@v4` — already current in TDM analyzer |

---

## Sources

- GitHub remote inspection (`git ls-remote https://github.com/saleae/AnalyzerSDK.git`) — SDK HEAD commit hash, branch list, tag list. HIGH confidence.
- https://github.com/saleae/AnalyzerSDK/commits/master — Commit history; last commit Jul 7, 2023. HIGH confidence.
- https://github.com/saleae/SampleAnalyzer/commits/master — Recent changes including auto_ptr removal (Aug 2024), Actions v4 (Oct 2024). HIGH confidence.
- https://github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerResults.h — FrameV2 API confirmed in header, no AddExportOption in AnalyzerResults. HIGH confidence.
- https://github.com/saleae/AnalyzerSDK/blob/master/include/Analyzer.h — `UseFrameV2()` confirmed present. HIGH confidence.
- https://ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/ — Custom export still Open/unimplemented. HIGH confidence.
- https://discuss.saleae.com/t/export-formats-for-low-level-analyzers/1040 — Saleae confirmed custom export not supported in Logic 2. HIGH confidence.
- https://support.saleae.com/product/user-guide/extensions-apis-and-sdks/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk — FrameV2 requirements (Logic 2.3.43+), implementation steps. HIGH confidence.
- https://ideas.saleae.com/f/changelog/ and https://discuss.saleae.com/t/logic-2-4-40/3586 — Logic 2 current version (2.4.40, Dec 2025), no SDK changes in recent releases. HIGH confidence.
- https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md — Notes on what is and is not implemented in Logic 2 (LoadSettings removed, Packet/Transaction not implemented). MEDIUM confidence (page not directly fetchable; content obtained via search result excerpts).
- CMake official docs — GIT_TAG commit hash recommendation for reproducible builds. HIGH confidence.

---

*Stack research for: Saleae Logic 2 LLA plugin (C++ AnalyzerSDK)*
*Researched: 2026-02-23*
