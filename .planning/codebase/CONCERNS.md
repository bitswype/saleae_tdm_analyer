# Codebase Concerns

**Analysis Date:** 2026-02-23

## Tech Debt

**Unsafe C-style String Operations:**
- Issue: Extensive use of sprintf, strcpy, and strcat with fixed-size buffers throughout the codebase
- Files: `src/TdmAnalyzerResults.cpp`, `src/TdmAnalyzerSettings.cpp`, `src/TdmAnalyzer.cpp`
- Impact: Buffer overflow vulnerability risk. While current buffer sizes (80-256 bytes) accommodate current usage, error message combinations could exceed bounds if new errors are added. String formatting logic is fragile and error-prone.
- Fix approach: Replace sprintf/strcpy with snprintf/safer alternatives. Consider using std::string for dynamic string building or fixed-width buffer validation with explicit length checks.

**Disabled Warnings Without Documentation:**
- Issue: MSVC warnings disabled globally with pragmas but not documented
- Files: `src/TdmAnalyzerSettings.cpp` (line 8), `src/TdmAnalyzerResults.cpp` (line 11)
- Impact: Warning suppression masks potential issues; new developers unfamiliar with why warnings are disabled
- Fix approach: Add comments explaining why C4996 (unsafe function) warnings are disabled. Consider using safer alternatives or at least documenting the deliberate choice.

**Extended PCM Wave Header Implementation Disabled:**
- Issue: PCMExtendedWaveFileHandler class is fully implemented but commented out in GenerateWAV()
- Files: `src/TdmAnalyzerResults.cpp` (line 173: `//PCMExtendedWaveFileHandler wave_file_handler(...)`)
- Impact: Dead code that consumes maintenance burden; duplication of complex wave file logic with no clear migration path
- Fix approach: Either remove the extended header implementation entirely (cleanup) or add a setting to allow users to select header type. Current README notes extended headers don't open in Audacity anyway.

**Magic String in Settings Validation:**
- Issue: Hardcoded string "SaleaeTdmAnalyzer" used for settings archive identification
- Files: `src/TdmAnalyzerSettings.cpp` (line 268)
- Impact: Settings migration/versioning will be fragile if this string changes; no version identifier for backwards compatibility
- Fix approach: Add a version number to settings archive to enable future format changes. Include migration logic for old formats.

## Known Bugs

**Potential String Buffer Overflow in Error Message Concatenation:**
- Symptoms: Error string builds by concatenating sprintf results; if multiple errors occur, buffer could overflow
- Files: `src/TdmAnalyzer.cpp` (lines 311-329), `src/TdmAnalyzerResults.cpp` (lines 51-71, 490-510)
- Trigger: Frame with multiple concurrent error flags (SHORT_SLOT + MISSED_DATA + MISSED_FRAME_SYNC + BITCLOCK_ERROR)
- Workaround: Errors are typically mutually exclusive in practice, but no code enforces this
- Risk: High severity if triggered; silent buffer overflow possible

**Incomplete Frame Handling in Wave Export:**
- Symptoms: If decoded data contains fewer slots per frame than specified in settings, final frame is padded with undefined behavior
- Files: `src/TdmAnalyzerResults.cpp` (lines 180-189)
- Trigger: Frame with fewer slots than `mSlotsPerFrame` setting
- Workaround: README documents this behavior (line 107-114) but the wrapping logic could produce incorrect audio
- Risk: Medium - audio data integrity issue if slot count varies

## Security Considerations

**No Input Validation on WAV Export Parameters:**
- Risk: PCMWaveFileHandler constructor accepts arbitrary values for sample_rate, num_channels, bits_per_channel. Extreme values could cause memory issues or file format corruption
- Files: `src/TdmAnalyzerResults.cpp` (line 172)
- Current mitigation: Parameters come from analyzer settings UI (controlled), but no range checks in constructor
- Recommendations: Add bounds checking in PCMWaveFileHandler constructor (sample_rate > 0, channels <= 256, bits_per_channel in valid range). Validate mTdmFrameRate in TdmAnalyzerSettings before use.

**Unchecked File Write Operations:**
- Risk: GenerateWAV() opens file without checking if disk is full; partial writes not detected
- Files: `src/TdmAnalyzerResults.cpp` (lines 166-193)
- Current mitigation: None - relies on OS to fail gracefully
- Recommendations: Check file stream state after writes (mFile.good()). Handle write failures explicitly.

## Performance Bottlenecks

**String Formatting in Frame Processing Loop:**
- Problem: sprintf calls happen for every frame during FrameV2 generation
- Files: `src/TdmAnalyzer.cpp` (lines 311-329)
- Cause: Error/warning strings are built with sprintf for each slot analyzed, even when not displayed
- Improvement path: Build error strings lazily only when needed for display. Cache string results per unique flag combination.

**Wave File Header Updates Every 10ms:**
- Problem: updateFileSize() seeks and rewrites header for every 10ms of audio data processed
- Files: `src/TdmAnalyzerResults.cpp` (lines 277, 410)
- Cause: Defensive programming to preserve data if export is cancelled, but creates excessive disk I/O
- Improvement path: Update header only on completion or cancel request, not periodically. Or increase update interval significantly.

**Linear Search in mFrameBits Cycling:**
- Problem: GetNextFrameBit() uses linear index increment with modulo check every bit
- Files: `src/TdmSimulationDataGenerator.cpp` (lines 112-119)
- Cause: Simple implementation works but repeated modulo operations on every bit generation
- Improvement path: Use bitwise AND with (mFrameBits.size() - 1) if size is power of 2, or pre-compute next index

## Fragile Areas

**TdmAnalyzer::GetTdmFrame() Bit Collection Logic:**
- Files: `src/TdmAnalyzer.cpp` (lines 97-160)
- Why fragile: Complex frame synchronization logic with multiple conditional branches for BITS_SHIFTED_RIGHT_1 mode. State machine implemented through nested loops with similar code blocks. Changes to frame detection logic require updates in two places (SetupForGettingFirstTdmFrame and GetTdmFrame).
- Safe modification: Add comprehensive unit tests for frame boundary detection. Use extraction to separate function for frame sync detection logic.
- Test coverage: No test files visible; integration testing only

**AnalyzeTdmSlot() Data Extraction Logic:**
- Files: `src/TdmAnalyzer.cpp` (lines 214-344)
- Why fragile: Bit extraction uses two different loops (MSB-first vs LSB-first) with similar structure. Any change to bit ordering logic risks breaking one variant. Flag clearing and re-initialization happens in multiple places.
- Safe modification: Extract bit extraction into separate helper functions (ExtractBitsAsMsbFirst, ExtractBitsAsLsbFirst). Add assertions for invariants (num_bits_to_process >= starting_index).
- Test coverage: None visible

**Wave File Header Structure Packing:**
- Files: `src/TdmAnalyzerResults.h` (lines 45-94)
- Why fragile: Raw binary struct with pragma pack(1) and scalar_storage_order directives. Changes to field order, type, or size will corrupt file format. Multiple pragma directives required for different compilers.
- Safe modification: Add static_assert to verify struct size (44 bytes for standard, 80 bytes for extended). Document byte offsets in comments. Use explicit serialization functions instead of raw struct writes.
- Test coverage: Manual testing only; no automated validation of written format

## Scaling Limits

**Frame Data Vector Allocation:**
- Current capacity: Unbounded growth of mDataBits, mDataValidEdges, mDataFlags vectors during frame processing
- Limit: Memory usage scales with longest frame before sync; theoretical max 256 slots * 64 bits/slot = 16,384 entries per vector. At ~8 bytes per entry = 131KB per vector, well within limits for single analysis, but accumulation of multiple concurrent analyses could stress memory.
- Scaling path: Vectors are cleared per-frame (lines 104-106, 339-341), so no accumulation issue. Current design is acceptable for stated 256 slots max.

**WAV File Header Update Frequency:**
- Current capacity: Updates every 10ms of audio data; at 48kHz this is 480 samples per update, which is performant
- Limit: For high sample rates (>192kHz) and many channels (256), this could cause excessive seeking. Each update does 3 seek + write operations.
- Scaling path: Increase update interval to 100ms or larger, or implement buffered header writes

## Fragile Analyzers

**Frame Rate Settings Not Validated Against Sample Rate:**
- Issue: User can set mTdmFrameRate to any value without checking if sample rate is sufficient
- Files: `src/TdmAnalyzer.cpp` (line 39), `src/TdmAnalyzerSettings.cpp` (line 228)
- Problem: mDesiredBitClockPeriod calculation could produce nonsensical values if TdmFrameRate > SampleRate / (SlotsPerFrame * BitsPerSlot * 4). GetMinimumSampleRateHz() requirement is calculated but not enforced before analysis starts.
- Fix: Add validation in SetSettingsFromInterfaces() to reject invalid combinations

**TdmBitAlignment Enum Naming Confusion:**
- Issue: BITS_SHIFTED_RIGHT_1 is actually "TDM typical, DSP mode A" per comment in settings (line 104)
- Files: `src/TdmAnalyzerSettings.h` (line 14), throughout codebase
- Problem: Enum name doesn't describe what it does; new maintainers will be confused about which mode to use
- Fix: Rename to DSP_MODE_A / DSP_MODE_B or RIGHT_SHIFTED_BY_ONE / NO_SHIFT

## Missing Critical Features

**No Backwards Compatibility for Settings Format:**
- Problem: If settings binary format changes (new fields added, old fields removed), old settings files will fail to load
- Files: `src/TdmAnalyzerSettings.cpp` (lines 261-369)
- Blocks: Can't evolve analyzer settings without breaking user configurations
- Fix: Add version number to settings archive. Implement migration functions for format changes. Document settings format versioning scheme.

**No Validation of Data Bit Count vs Slot Configuration:**
- Problem: User can set mDataBitsPerSlot > mBitsPerSlot (validated on line 233-237), but inverse check that slots fit within frame not present
- Files: `src/TdmAnalyzerSettings.cpp` (line 233)
- Blocks: Can't add warning for impractical configurations (e.g., 256 slots * 64 bits = unrealistic data rates)
- Fix: Add warnings in SetSettingsFromInterfaces() for combinations that suggest user error

## Test Coverage Gaps

**No Unit Tests for Core Analysis Logic:**
- What's not tested: GetTdmFrame(), AnalyzeTdmSlot(), bit extraction with different settings combinations
- Files: `src/TdmAnalyzer.cpp` (entire file)
- Risk: Regressions in frame sync detection or bit ordering logic go undetected until user reports corrupted audio
- Priority: High - core functionality that directly affects output quality

**No Tests for Wave File Generation:**
- What's not tested: PCMWaveFileHandler with all sample bit depths (8, 16, 32, 40, 48, 64 bits), channel counts up to 256, edge cases like single-channel or single-frame exports
- Files: `src/TdmAnalyzerResults.cpp` (PCMWaveFileHandler class)
- Risk: Corrupted audio exports only discovered by end users; header format errors not caught
- Priority: High - affects user deliverables

**No Tests for Settings Serialization:**
- What's not tested: SaveSettings/LoadSettings with all valid setting combinations, settings migration from old formats
- Files: `src/TdmAnalyzerSettings.cpp` (lines 261-398)
- Risk: User projects break when upgrading analyzer versions
- Priority: Medium - affects user experience on updates

**No Tests for Simulation Data Generation:**
- What's not tested: GenerateSimulationData() with various configuration combinations, edge cases like 1 slot or 256 slots
- Files: `src/TdmSimulationDataGenerator.cpp`
- Risk: Simulation mode used for verification could hide real bugs
- Priority: Low - mainly used for development/testing

---

*Concerns audit: 2026-02-23*
