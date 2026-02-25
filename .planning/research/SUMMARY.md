# Project Research Summary

**Project:** Saleae TDM Analyzer — v1.4 SDK & Export Modernization
**Domain:** Saleae Logic 2 C++ Protocol Analyzer Plugin (TDM/I2S audio)
**Researched:** 2026-02-24
**Confidence:** HIGH

## Executive Summary

The v1.4 milestone targets four improvements to an existing, working Saleae Logic 2 protocol analyzer plugin: RF64 WAV export support, FrameV2 data enrichment, a sample rate sanity check, and an SDK audit. Research against actual source code and official SDK headers revealed that two of these four items are already partially or fully implemented — the SDK is already at latest HEAD (`114a3b8`), and FrameV2 is fully wired up but lacks boolean error fields and has a field key naming issue (`"frame #"` contains a space and hash character that can break HLA scripts). The two genuine new deliverables are the RF64 handler (replacing a 4 GiB hard limit that currently writes a plain-text error instead of a WAV file) and the sample rate check annotation.

The recommended approach is a build order of: SDK audit documentation first (no code changes — just verify and document), then FrameV2 enrichment (in-place modification with lowest risk), then sample rate check (additive, non-blocking via WorkerThread annotation rather than SetErrorText), then RF64 handler last (most surface area: new struct, new class, GenerateWAV modification). This order ensures each phase builds on a known-good foundation before tackling the largest change.

The primary risk is the RF64 implementation, which has multiple byte-level precision requirements: ds64 chunk at the correct offset (byte 12), sentinel values of `0xFFFFFFFF` at specific positions (offsets 4 and 76), U64 arithmetic throughout for frame/sample counters and ds64 fields, and a seek-back to update ds64 at file close. The secondary risk is the sample rate check: the SDK provides `SetErrorText()` which is blocking (prevents analysis from running), but the feature requires non-blocking advisory behavior — the warning must go into WorkerThread output (FrameV2 or tabular text), not into `SetSettingsFromInterfaces()`. Both risks have clear avoidance strategies documented in PITFALLS.md and ARCHITECTURE.md.

---

## Key Findings

### Recommended Stack

The stack is fully locked: C++11, Saleae AnalyzerSDK `114a3b8` (confirmed as current master HEAD — no update exists as of 2026-02-24), CMake 3.13+, standard library only (no external dependencies). RF64 is implemented via a new packed struct and handler class following the same pattern as the existing `PCMWaveFileHandler` — this is the correct approach per EBU TECH 3306, confirmed against libsndfile and FFmpeg reference implementations. No third-party WAV libraries are appropriate.

**Core technologies:**
- **AnalyzerSDK `114a3b8`** — plugin base classes, FrameV2 API; already at latest HEAD, no update needed
- **C++11** — mandated by SDK; `#pragma pack(1)`, `static_assert`, `constexpr` all available
- **CMake 3.13+ with FetchContent** — build system; SDK pin is correct and reproducible
- **RF64 EBU TECH 3306** — 80-byte packed struct (`WaveRF64Header`), always-RF64 write strategy (no JUNK-to-ds64 upgrade path)
- **FrameV2 SDK API** — `AddBoolean()`, `AddInteger()`, `AddString()`; all confirmed present in the pinned SDK

### Expected Features

**Must have (table stakes for v1.4):**
- **RF64 WAV export** — replaces the 4 GiB guard that currently writes a text error to the .wav path; highest user-visible impact change in this milestone
- **FrameV2 boolean error fields** — five `AddBoolean()` calls per slot enabling clean HLA filtering without string parsing
- **Sample rate warning annotation** — non-blocking advisory on first analysis frame when sample rate is below the 4x bit clock floor
- **SDK audit documentation** — confirms `114a3b8` is HEAD; no code changes required, but a commit documenting the audit closes the milestone task

**Should have (differentiators within v1.4 scope):**
- **Rename `"frame #"` to `"frame_number"`** — eliminates HLA breakage from the space/hash in the key name
- **Add 1-based `"slot"` field alongside 0-based `"channel"`** — human-readable in the data table
- **Remove unused `mResultsFrameV2` member from `TdmAnalyzer.h`** — dead state that creates confusion

**Defer to later milestone:**
- Named protocol presets (I2S, LJ, RJ, DSP) — settings dialog work, separate concern
- Python HLA companion — separate project, out of plugin scope
- Settings format version field — needed only if a persistent new settings field is added; v1.4 does not add one

### Architecture Approach

The plugin architecture is fixed by the Saleae SDK: `Analyzer` subclass owns the analysis thread, `AnalyzerResults` subclass owns export and display, `AnalyzerSettings` subclass owns configuration. All four v1.4 features integrate cleanly into specific, well-bounded locations without cross-cutting concerns. RF64 is confined to `TdmAnalyzerResults.h/.cpp` (new struct + new class + `GenerateWAV()` modification). FrameV2 enrichment is confined to `TdmAnalyzer.cpp:AnalyzeTdmSlot()` lines 299-337. Sample rate check goes into `TdmAnalyzerSettings.cpp:SetSettingsFromInterfaces()` for hard rejection only, and optionally into `TdmAnalyzer.cpp:WorkerThread()` for advisory annotation.

**Major components and their v1.4 changes:**
1. **`TdmAnalyzerSettings.cpp`** — add hard-block for physically impossible settings (required sample rate > 500 MSPS using U64 arithmetic to avoid overflow); no soft warning here
2. **`TdmAnalyzer.cpp:AnalyzeTdmSlot()`** — add 5 boolean error fields, rename `"frame #"` to `"frame_number"`, add 1-based `"slot"` field; remove unused `mResultsFrameV2` member from `TdmAnalyzer.h`
3. **`TdmAnalyzerResults.h/.cpp`** — add `WaveRF64Header` struct (80 bytes, with `static_assert`), add `RF64WaveFileHandler` class with U64 frame counters, modify `GenerateWAV()` to remove the 4 GiB guard and instantiate `RF64WaveFileHandler`

### Critical Pitfalls

1. **RF64 ds64 chunk at wrong byte offset** — `ds64` must immediately follow `WAVE` at byte 12; compliant readers scan for it at a fixed offset and will fail to find it if out of position. Avoid by using a single packed struct with a `static_assert` on its size, verified with `ffprobe` and a hex dump. (PITFALLS.md Pitfall 1)

2. **RF64 size fields not set to `0xFFFFFFFF` sentinel** — both the RIFF chunk size (offset 4) and data chunk size (offset 76) must be `0xFFFFFFFF` as the sentinel, not zero or the truncated real size. Define `constexpr U32 RF64_SIZE_SENTINEL = 0xFFFFFFFFu` and use it in the struct defaults. (PITFALLS.md Pitfall 2)

3. **RF64 ds64 not updated at file close with U64 arithmetic** — the seek-back to offsets 20, 28, 36 must write 8-byte little-endian values; copying the PCMWaveFileHandler's U32 arithmetic silently writes wrong ds64 values. Use `writeLittleEndianData(value, 8)` for all three ds64 fields in `close()`. (PITFALLS.md Pitfall 3)

4. **`SetErrorText()` used for sample rate advisory** — calling this in `SetSettingsFromInterfaces()` blocks the user from running analysis. The advisory must be non-blocking: emit it via FrameV2 or tabular text in `WorkerThread()`. `SetErrorText()` returning `false` is only appropriate for physically impossible configurations. (PITFALLS.md Pitfall 7)

5. **FrameV2 fields added inconsistently** — every field must be emitted for every slot frame or the data table shows sparse columns. Always add all fields with empty string or zero values even when there is nothing to report. (PITFALLS.md Pitfall 5)

---

## Implications for Roadmap

Based on research, the v1.4 milestone maps naturally to four phases in dependency order. All four features are independent of each other but the build sequence minimizes risk and ensures each phase can be verified in Logic 2 before the next phase adds surface area.

### Phase 1: SDK Audit and Housekeeping

**Rationale:** Closes the SDK update task with zero code risk (no update exists), adds protective comments, removes dead code, and fixes the FrameV2 key naming issue before FrameV2 enrichment in Phase 2. A clean codebase reduces cognitive load in all subsequent phases.
**Delivers:** Confirmed SDK state documented in git; `UseFrameV2()` comment guard added; unused `mResultsFrameV2` member removed from `TdmAnalyzer.h`; `"frame #"` renamed to `"frame_number"` in the existing FrameV2 block.
**Addresses:** SDK audit feature; FrameV2 field key naming fix; dead code elimination.
**Avoids:** Pitfall 4 (UseFrameV2 accidentally removed during SDK work); Pitfall 5 (field key with space/hash); Pitfall 9 (no ABI break since no SDK update).

### Phase 2: FrameV2 Enrichment

**Rationale:** Purely additive change to a single function (`AnalyzeTdmSlot()`) with no new files and no struct changes. Lowest risk of the substantive features. Verifiable immediately in Logic 2's data table and via HLA testing.
**Delivers:** Five new boolean error fields per slot (`short_slot`, `extra_slot`, `data_error`, `frame_sync_missed`, `bitclock_error`); 1-based `"slot"` field alongside 0-based `"channel"`; all fields emitted consistently on every frame including error-free frames.
**Uses:** `FrameV2::AddBoolean()` from SDK (confirmed present in `114a3b8`); existing `mResultsFrame.mFlags` bitmask constants.
**Avoids:** Pitfall 5 (always emit all fields regardless of error state); ARCHITECTURE.md anti-pattern of adding `AddFrameV2()` calls after `CommitResults()`.

### Phase 3: Sample Rate Sanity Check

**Rationale:** Additive to `SetSettingsFromInterfaces()` (hard-block only for impossible configs) and optionally to `WorkerThread()` (advisory FrameV2 annotation). No new files, no header struct changes. The critical design decision — blocking vs. non-blocking — must be resolved before implementation to avoid shipping the wrong behavior.
**Delivers:** Hard rejection in `SetSettingsFromInterfaces()` for physically impossible configurations (required rate > 500 MSPS) using U64 arithmetic; advisory FrameV2 annotation on first frame when actual sample rate < `GetMinimumSampleRateHz()`.
**Avoids:** Pitfall 7 (SetErrorText blocks analysis — only use for > 500 MSPS case); Pitfall 8 (threshold derived from `GetMinimumSampleRateHz()` at 4x bit clock, not recomputed independently); Pitfall 6 (no new persistent settings field required, so SimpleArchive version bump is not triggered).

### Phase 4: RF64 WAV Export

**Rationale:** Highest surface area — new struct, new class (~130 lines), and modification to `GenerateWAV()`. Placed last so it builds on a stable, fully-verified codebase. The 4 GiB guard removal is a destructive change (the existing text-error fallback disappears entirely), so confidence in the RF64 implementation must be high before merging.
**Delivers:** `WaveRF64Header` struct (80 bytes, with `static_assert`); `RF64WaveFileHandler` class with U64 frame/sample counters and U64 seek-back in `close()`; `GenerateWAV()` modified to use `RF64WaveFileHandler`; 4 GiB guard block removed.
**Uses:** `#pragma pack(1)`, `static_assert`, existing `writeLittleEndianData()` helper with `num_bytes=8`, `seekp()` for ds64 update at offsets 20/28/36; `constexpr U32 RF64_SIZE_SENTINEL = 0xFFFFFFFFu`.
**Avoids:** Pitfall 1 (ds64 at correct byte offset — verified by hex dump after writing known-size synthetic data); Pitfall 2 (sentinel values); Pitfall 3 (U64 seek-back at close with correct offset constants); ARCHITECTURE.md anti-pattern of keeping the 4 GiB guard alongside RF64; ARCHITECTURE.md anti-pattern of using U32 for RF64 frame counters.

### Phase Ordering Rationale

- Phase 1 first because the SDK audit is a no-op code change and housekeeping reduces noise in all later diffs. The key rename and dead member removal are small enough to bundle here.
- Phase 2 before Phase 3 and 4 because it touches only `AnalyzeTdmSlot()` — a single function with no new dependencies. Fully verifiable in Logic 2 data table before anything else changes.
- Phase 3 before Phase 4 because it touches settings and WorkerThread, which are independent of the WAV export path. Sample rate check bugs are found in isolation, not mixed with RF64 debugging.
- Phase 4 last because it is the highest-risk change (permanently removes the existing 4 GiB fallback mechanism) and requires byte-level verification against the EBU TECH 3306 spec.

### Research Flags

Phases with well-documented patterns (skip research-phase):
- **Phase 1:** Straightforward housekeeping; no new patterns required. SDK audit result is already known (no update available).
- **Phase 2:** `AddBoolean()` is confirmed in the SDK header; usage pattern is identical to existing `AddInteger()` calls already in place.
- **Phase 3:** `SetErrorText()` and `GetMinimumSampleRateHz()` are confirmed in SDK headers; the blocking-vs-non-blocking design decision is resolved in PITFALLS.md Pitfall 7.
- **Phase 4:** RF64 struct layout is fully specified in ARCHITECTURE.md and STACK.md, cross-referenced against libsndfile and FFmpeg. No additional research needed before implementation.

No phase requires `/gsd:research-phase` during planning. All issues have been researched to resolution with HIGH confidence sources.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | SDK version confirmed by direct `git ls-remote` query; C++11 mandated by SDK; RF64 struct layout confirmed against libsndfile `src/rf64.c` and FFmpeg `libavformat/wavenc.c` reference implementations |
| Features | HIGH | All four v1.4 features scoped against confirmed SDK capabilities; existing FrameV2 and sample rate implementations verified in actual source code at specific line numbers; RF64 vs W64 tool compatibility matrix verified |
| Architecture | HIGH | All component boundary findings verified against actual source files at specific line numbers; no assumptions made about code that was not directly inspected |
| Pitfalls | HIGH | Pitfalls grounded in direct source code inspection; RF64 pitfalls cross-referenced against EBU TECH 3306, libsndfile implementation blog, and PlayPcmWin documentation |

**Overall confidence:** HIGH

### Gaps to Address

- **`"frame_number"` vs `"frame_num"` key name conflict:** ARCHITECTURE.md recommends `"frame_number"`, PITFALLS.md recommends `"frame_num"`. Both improve on `"frame #"`. Resolve in Phase 1 by checking Saleae's CAN analyzer field names (confirmed to use full words: `"identifier"`, `"num_data_bytes"`), then pick one and document it in the code comment.

- **Always-RF64 vs JUNK-to-ds64 strategy terminology:** STACK.md recommends always writing RF64 headers directly. FEATURES.md describes the JUNK-to-ds64 upgrade pattern under "differentiators." Both sources conclude the same implementation choice (always write RF64), but the JUNK chunk description in FEATURES.md may cause confusion. The Phase 4 plan should explicitly state: always write `RF64` RIFF ID and `ds64` from file open — no JUNK chunk, no upgrade path.

- **`updateFileSize()` periodic interval for RF64:** PITFALLS.md Performance Traps recommends increasing the update interval to 1000ms for RF64 vs the existing ~100ms (every `mSampleRate/100` frames) for WAV. The Phase 4 plan should specify the update interval explicitly rather than copying the existing value without review.

- **Settings archive version field (conditional):** PITFALLS.md Pitfall 6 recommends adding a `U32 version` field if any new persistent settings field is added. Phase 3's sample rate check does not add a persistent settings field (the check is computed at runtime from existing settings). If Phase 3 implementation reveals a need for a new toggle setting, the version field must be added before shipping. This is a conditional gap that needs confirmation during Phase 3 planning.

---

## Sources

### Primary (HIGH confidence)
- `src/TdmAnalyzer.cpp`, `src/TdmAnalyzerResults.h/.cpp`, `src/TdmAnalyzerSettings.cpp`, `cmake/ExternalAnalyzerSDK.cmake` — direct source inspection; all architecture findings and pitfalls grounded here at specific line numbers
- `git ls-remote https://github.com/saleae/AnalyzerSDK.git` — SDK HEAD confirmed `114a3b8`, no newer commits
- `https://raw.githubusercontent.com/saleae/AnalyzerSDK/master/include/AnalyzerResults.h` — `FrameV2` class, `AddBoolean()`, `AddFrameV2()` signatures
- `https://raw.githubusercontent.com/saleae/AnalyzerSDK/master/include/Analyzer.h` — `UseFrameV2()`, `GetSampleRate()`, `GetMinimumSampleRateHz()` signatures
- `https://github.com/erikd/libsndfile/blob/master/src/rf64.c` — RF64 ds64 chunk layout reference implementation
- `https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/wavenc.c` — RF64 write pattern, 28-byte ds64 payload confirmation
- `https://github.com/saleae/can-analyzer/blob/master/src/CanAnalyzer.cpp` — FrameV2 field naming conventions (lowercase underscore)
- `https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md` — `SetErrorText`, `GetMinimumSampleRateHz` patterns

### Secondary (MEDIUM confidence)
- `https://support.saleae.com/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk` — FrameV2 feature scope and field design patterns
- `https://markheath.net/post/naudio-rf64-bwf` — JUNK-to-ds64 complexity and testing difficulty confirmation
- `https://sourceforge.net/p/playpcmwin/wiki/RF64%20WAVE/` — sentinel value and chunk layout corroboration
- `https://forum.cockos.com/archive/index.php/t-191986.html` — RF64 vs W64 compatibility comparison (RF64 wins)
- `http://www.mega-nerd.com/erikd/Blog/CodeHacking/libsndfile/rf64_specs.html` — ds64 RIFFSize update pitfall documentation
- `https://forum.audacityteam.org/t/rf64-with-4gb-size-is-mistaken-as-wav/54380` — Audacity RF64 fragility evidence

### Tertiary (LOW confidence)
- `https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf` — EBU TECH 3306 official RF64 spec; PDF access limited but structure confirmed via reference implementations above

---
*Research completed: 2026-02-24*
*Ready for roadmap: yes*
