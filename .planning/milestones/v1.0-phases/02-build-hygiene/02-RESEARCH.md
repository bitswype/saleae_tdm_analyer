# Phase 2: Build Hygiene - Research

**Researched:** 2026-02-24
**Domain:** CMake FetchContent pinning; C++ static_assert struct size verification
**Confidence:** HIGH

## Summary

Phase 2 consists of exactly two mechanical changes with no ambiguity. Both changes are one-to-several lines, require no new libraries, and have fully specified correct implementations already documented in the project's prior research.

BILD-01 is a one-line change in `cmake/ExternalAnalyzerSDK.cmake`: replace `GIT_TAG master` with the known-good commit hash `114a3b8306e6a5008453546eda003db15b002027`. The current `master` reference causes non-reproducible builds because any future SDK push changes what a fresh clone fetches. The correct hash was identified during the project research phase via `git ls-remote` on the SDK repository. With `GIT_SHALLOW True` remaining in place, the build process is otherwise unchanged — FetchContent still works the same way, just pointed at a fixed object.

BILD-02 adds two `static_assert` statements to `src/TdmAnalyzerResults.h`, immediately after each WAV header struct definition. The current code uses `#pragma scalar_storage_order little-endian` to express endianness intent, but this pragma is a GCC-only extension — Clang (used on macOS) and MSVC (used on Windows) silently ignore it. No `static_assert` currently guards the struct sizes, so any accidental padding introduced by a future change, or any compiler that doesn't honor `#pragma pack`, would produce malformed WAV headers silently. The correct expected sizes are 44 bytes for `WavePCMHeader` and 80 bytes for `WavePCMExtendedHeader`, derived directly from the WAV PCM specification and confirmed by the byte-offset comments in the struct definitions.

**Primary recommendation:** Two independent commits — one for BILD-01 (CMake pin), one for BILD-02 (static_assert guards). Both can be planned as a single wave since they touch entirely different files and have no dependency on each other.

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| BILD-01 | Pin AnalyzerSDK FetchContent to commit hash `114a3b8306e6a5008453546eda003db15b002027` instead of `master` branch | Hash identified via `git ls-remote` during project research (SUMMARY.md). File location confirmed: `cmake/ExternalAnalyzerSDK.cmake` line 17. One-line change from `GIT_TAG master` to the full hash. `GIT_SHALLOW True` is compatible with commit hash pinning. |
| BILD-02 | Add `static_assert` guards for `WavePCMHeader` (44 bytes) and `WavePCMExtendedHeader` (80 bytes) compile-time size verification | Expected sizes confirmed by the byte-offset comments in `src/TdmAnalyzerResults.h` (data begins @ 44 and @ 80 respectively). `#pragma scalar_storage_order` is GCC-only (LLVM issue #34641 confirms Clang does not implement it). Both structs use `#pragma pack(push, 1)` which is portable across GCC/Clang/MSVC. `static_assert` is C++11 standard. |
</phase_requirements>

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Saleae AnalyzerSDK | Commit `114a3b8` | External SDK fetched by CMake | Only SDK supported by Logic 2; pinning to this hash is the BILD-01 fix itself |
| CMake FetchContent | 3.11+ | SDK acquisition at configure time | Already in use; no change to mechanism, only to `GIT_TAG` value |
| C++11 `static_assert` | — | Compile-time struct size verification | Standard C++11; supported by all three target compilers (GCC, Clang, MSVC) |

### Supporting

No new libraries or dependencies are added in Phase 2. Both changes are configuration/assertion additions only.

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Commit hash pin | Git tag (e.g., `v2.3.58`) | The SDK does not use consistent version tags; the commit hash is more precise and less ambiguous |
| `static_assert` after struct | `sizeof()` check in a test | `static_assert` fires at compile time on every platform without any test infrastructure; preferable for a build hygiene requirement |
| `static_assert` on size only | `static_assert` + `offsetof` per field | Field offset assertions are thorough but overkill; size assertion is sufficient to catch any packing mistake |

**Installation:** No new dependencies. No install step.

---

## Architecture Patterns

### Relevant Project Structure

```
cmake/
└── ExternalAnalyzerSDK.cmake    # BILD-01: GIT_TAG on line 17

src/
└── TdmAnalyzerResults.h         # BILD-02: static_assert after each struct definition
```

### Pattern 1: FetchContent Commit Hash Pin

**What:** Replace a symbolic `GIT_TAG` reference (branch name) with an immutable object reference (full commit SHA).

**When to use:** Anytime a third-party dependency is fetched via `FetchContent_Declare` with a branch name.

**Example:**
```cmake
# BEFORE (non-reproducible):
FetchContent_Declare(
    analyzersdk
    GIT_REPOSITORY https://github.com/saleae/AnalyzerSDK.git
    GIT_TAG        master
    GIT_SHALLOW    True
    GIT_PROGRESS   True
)

# AFTER (reproducible):
FetchContent_Declare(
    analyzersdk
    GIT_REPOSITORY https://github.com/saleae/AnalyzerSDK.git
    GIT_TAG        114a3b8306e6a5008453546eda003db15b002027
    GIT_SHALLOW    True
    GIT_PROGRESS   True
)
```

**Note on GIT_SHALLOW with commit hash:** `GIT_SHALLOW True` works with commit hashes in CMake 3.11+. Git performs a shallow fetch of the specific commit. This is compatible with the existing configuration.

**Note on comment:** Add a comment above the `GIT_TAG` line documenting why a hash is used and what SDK version it corresponds to:
```cmake
# Pinned to the last known-good SDK commit (July 2023).
# Use the full hash for reproducible builds — branch names like 'master' are mutable.
# To upgrade: verify new commit builds successfully on all three platforms, then update this hash.
GIT_TAG        114a3b8306e6a5008453546eda003db15b002027
```

### Pattern 2: static_assert Struct Size Guard

**What:** Add a `static_assert` immediately after a packed struct definition to verify its total byte size at compile time. This catches any packing mistake the moment it is introduced, on every compiler, without requiring a test to run.

**When to use:** After any struct definition that uses `#pragma pack` for binary I/O, especially when the struct is written directly to a file or network stream.

**Example (directly applicable to this codebase):**
```cpp
// Source: WAV PCM specification — PCM header is 44 bytes
// (4 RIFF id + 4 RIFF size + 4 WAVE id + 4 fmt id + 4 fmt size +
//  2 format tag + 2 channels + 4 sample rate + 4 bytes/sec +
//  2 block align + 2 bits/sample + 4 data id + 4 data size = 44)
#pragma scalar_storage_order little-endian
#pragma pack(push, 1)
typedef struct { /* ... fields ... */ } WavePCMHeader;
#pragma pack(pop)
#pragma scalar_storage_order default

static_assert(sizeof(WavePCMHeader) == 44,
    "WavePCMHeader must be 44 bytes — check #pragma pack and field types. "
    "Note: #pragma scalar_storage_order is GCC-only; this assert verifies "
    "packing is correct on all compilers regardless of that pragma.");
```

```cpp
// Source: WAV EXTENSIBLE format specification — extended header is 80 bytes
// (44 standard fields + 2 extension size + 2 data bits + 4 channel mask +
//  16 SubFormat GUID + 4 fact id + 4 fact size + 4 sample length +
//  4 data id + 4 data size = 80)
#pragma scalar_storage_order little-endian
#pragma pack(push, 1)
typedef struct { /* ... fields ... */ } WavePCMExtendedHeader;
#pragma pack(pop)
#pragma scalar_storage_order default

static_assert(sizeof(WavePCMExtendedHeader) == 80,
    "WavePCMExtendedHeader must be 80 bytes — check #pragma pack and field types. "
    "Note: #pragma scalar_storage_order is GCC-only; this assert verifies "
    "packing is correct on all compilers regardless of that pragma.");
```

**Placement rule:** The `static_assert` must appear AFTER `#pragma pack(pop)` and `#pragma scalar_storage_order default`. Placing it inside the pack region is unnecessary; `sizeof` is evaluated at compile time and reports the final packed size.

### Anti-Patterns to Avoid

- **Do NOT use `GIT_TAG HEAD`** — this is equally non-reproducible. Only a full 40-character commit SHA provides immutability.
- **Do NOT add `static_assert` inside the struct body** — C++11 allows `static_assert` inside struct bodies but it is less readable. Place it immediately after the closing brace.
- **Do NOT remove `#pragma scalar_storage_order`** — while it is GCC-only, it documents intent and may provide actual enforcement on GCC builds. The `static_assert` augments it, not replaces it.
- **Do NOT change `GIT_SHALLOW True`** — shallow fetches work with commit hashes in CMake 3.11+ and speed up configure time. Leave it in place.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Struct size verification | Runtime check in test code | `static_assert` | Compile-time; no test infrastructure needed; fires on every platform every build |
| Reproducible SDK version | Local copy of SDK source | FetchContent with pinned hash | Hash pinning gives reproducibility without duplicating the SDK in the repo |

**Key insight:** Both BILD requirements are solved by standard C++ and CMake primitives that have been available since C++11 and CMake 3.11 respectively. No custom infrastructure is needed.

---

## Common Pitfalls

### Pitfall 1: Short Hash Instead of Full SHA

**What goes wrong:** Using the 7-character abbreviated hash (`114a3b8`) instead of the full 40-character SHA in `GIT_TAG`. CMake FetchContent and git may resolve short hashes differently depending on the repository's object count; full SHAs are unambiguous.

**How to avoid:** Always use the full 40-character SHA: `114a3b8306e6a5008453546eda003db15b002027`.

**Warning signs:** `GIT_TAG 114a3b8` (7 chars) rather than `GIT_TAG 114a3b8306e6a5008453546eda003db15b002027` (40 chars).

### Pitfall 2: static_assert Placed Before #pragma pack(pop)

**What goes wrong:** If `static_assert(sizeof(WavePCMHeader) == 44, ...)` is placed inside the pack region (between `#pragma pack(push, 1)` and `#pragma pack(pop)`), the size is still correctly evaluated, but placement is confusing and non-idiomatic.

**How to avoid:** Place both `static_assert` statements after `#pragma scalar_storage_order default` — fully outside both pragmas.

### Pitfall 3: Asserting Wrong Expected Size

**What goes wrong:** Asserting `== 44` for `WavePCMExtendedHeader` (which is 80 bytes) or `== 80` for `WavePCMHeader` (which is 44 bytes). The assert would fire on first compile with a confusing message.

**How to avoid:** Cross-check sizes against the byte-offset comments in the struct definitions:
- `WavePCMHeader`: data begins `@ 44` (comment at bottom of struct) → assert `== 44`
- `WavePCMExtendedHeader`: data begins `@ 80` (comment at bottom of struct) → assert `== 80`

### Pitfall 4: GIT_SHALLOW Incompatibility Concern (Non-Issue)

**What might be feared:** That `GIT_SHALLOW True` combined with a commit hash won't work because shallow clones can't access arbitrary commits.

**Why it is not a problem:** CMake FetchContent performs a shallow fetch directly of the specified commit object when `GIT_SHALLOW True` is used with a commit SHA. This has worked since CMake 3.11 with Git 2.11+. The existing CI and local build environments already meet this requirement. No change to `GIT_SHALLOW True` is needed.

---

## Code Examples

Verified patterns from source inspection and WAV specification:

### BILD-01: Complete cmake/ExternalAnalyzerSDK.cmake Change

```cmake
# BEFORE (line 17):
GIT_TAG        master

# AFTER (line 17, with added comment above):
# Pinned to the last known-good SDK commit (July 2023, hash verified via git ls-remote).
# Use the full SHA for reproducible builds — branch names like 'master' are mutable.
# To upgrade: verify new commit builds on all three platforms, then update this hash.
GIT_TAG        114a3b8306e6a5008453546eda003db15b002027
```

The surrounding `FetchContent_Declare` block is otherwise unchanged.

### BILD-02: static_assert Insertions in TdmAnalyzerResults.h

**After WavePCMHeader (currently line 64):**
```cpp
} WavePCMHeader;
#pragma pack(pop)
#pragma scalar_storage_order default

// Verify WavePCMHeader is the correct size for WAV PCM format.
// Note: #pragma scalar_storage_order is GCC-only and ignored by Clang/MSVC.
// This assert catches packing errors on all supported compilers.
static_assert(sizeof(WavePCMHeader) == 44,
    "WavePCMHeader must be 44 bytes per WAV PCM spec. "
    "Check #pragma pack(1) is in effect and all field types are correct.");
```

**After WavePCMExtendedHeader (currently line 94):**
```cpp
} WavePCMExtendedHeader;
#pragma pack(pop)
#pragma scalar_storage_order default

// Verify WavePCMExtendedHeader is the correct size for WAV EXTENSIBLE format.
// Note: #pragma scalar_storage_order is GCC-only and ignored by Clang/MSVC.
// This assert catches packing errors on all supported compilers.
static_assert(sizeof(WavePCMExtendedHeader) == 80,
    "WavePCMExtendedHeader must be 80 bytes per WAV EXTENSIBLE spec. "
    "Check #pragma pack(1) is in effect and all field types are correct.");
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `GIT_TAG master` for FetchContent | `GIT_TAG <full-SHA>` for reproducibility | Whenever build hygiene is first applied | Fresh clones always build the same SDK version |
| No struct size verification | `static_assert(sizeof(S) == N)` after packed struct | C++11 (2011) | Compiler rejects mismatched packing on all platforms; no runtime test needed |

**Deprecated/outdated:**
- `GIT_TAG master` / `GIT_TAG main`: Non-reproducible. Fine for rapid prototyping, never acceptable for a distributed plugin. The correct replacement is a full commit SHA.
- Relying solely on `#pragma scalar_storage_order` for endianness correctness: GCC-only, silently ignored elsewhere. Must be paired with `static_assert` on struct size.

---

## Open Questions

1. **Does `GIT_SHALLOW True` + full SHA work with GitHub's smart HTTP transport?**
   - What we know: CMake documentation and Git documentation both confirm shallow fetches of specific commits are supported in Git 2.11+ via `--filter=blob:none` or direct commit fetch. All three CI platforms (Ubuntu, macOS, Windows) use Git well above this version.
   - What's unclear: Whether any GitHub rate limiting or transport restriction could interfere.
   - Recommendation: No concern in practice — this pattern is used in thousands of CMake projects. Proceed without investigation.

2. **Should CLAUDE.md document the pinned SDK version?**
   - What we know: The project has no `CLAUDE.md` in the repo root (the global `CLAUDE.md` from `~/.claude/CLAUDE.md` notes this is a requirement for every project).
   - What's unclear: Whether BILD-01 should include creating a repo-level `CLAUDE.md` as a side task.
   - Recommendation: The planner should note that creating a project `CLAUDE.md` is a documentation obligation per global instructions, but it belongs in Phase 3 (Documentation) not Phase 2. BILD-01's comment block in the CMake file is sufficient documentation for the SDK pin itself.

---

## Sources

### Primary (HIGH confidence)

- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/cmake/ExternalAnalyzerSDK.cmake` — confirmed `GIT_TAG master` at line 17, `GIT_SHALLOW True` at line 18
- Direct source inspection: `/home/chris/gitrepos/saleae_tdm_analyer/src/TdmAnalyzerResults.h` — confirmed both struct definitions, byte-offset comments showing 44 and 80, pragma placement, no existing `static_assert`
- `.planning/research/SUMMARY.md` — SDK commit hash `114a3b8306e6a5008453546eda003db15b002027` identified via `git ls-remote` (HIGH confidence, derived from external verification)
- `.planning/research/PITFALLS.md` — Pitfall 3 (`scalar_storage_order` GCC-only) and Pitfall 7 (`GIT_TAG master` instability) with full rationale and fix specifications
- LLVM issue tracker: [Implement 'scalar_storage_order' attribute · Issue #34641 · llvm/llvm-project](https://github.com/llvm/llvm-project/issues/34641) — Clang does not implement this pragma (confirmed, cited in PITFALLS.md)
- WAV PCM specification: [WAVE PCM soundfile format](http://soundfile.sapp.org/doc/WaveFormat/) — confirms standard PCM header is 44 bytes (HIGH confidence)

### Secondary (MEDIUM confidence)

- CMake FetchContent documentation — `GIT_SHALLOW` + commit hash compatibility confirmed in CMake 3.11+ documentation; not re-fetched for this research but consistent with all prior project research findings

---

## Metadata

**Confidence breakdown:**
- BILD-01 fix (commit hash, file location): HIGH — confirmed by direct file read; hash from verified `git ls-remote` output in prior research
- BILD-02 fix (static_assert sizes): HIGH — sizes confirmed by byte-offset comments in the struct definitions themselves; `static_assert` is C++11 standard with no version ambiguity
- Pitfalls (shallow+hash, wrong size): HIGH — derived from CMake and C++ specification behavior, not external ecosystem state

**Research date:** 2026-02-24
**Valid until:** Indefinite — findings are grounded in static source code and stable language/tool specifications
