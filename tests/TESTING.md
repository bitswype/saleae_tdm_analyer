# TDM Analyzer Test Suite

## Overview

The C++ correctness test suite verifies the TDM Low Level Analyzer's decode logic by generating synthetic TDM signals, running the analyzer via the SDK testlib, and asserting decoded output matches expected values.

**What it tests:** LLA decode accuracy - value correctness, error flag detection, FrameV2 field population, and robustness under misconfiguration.

**What it does not test:** GenerateBubbleText/GenerateTabularText (formatting logic), CSV/WAV export, settings validation (SetSettingsFromInterfaces), LoadSettings/SaveSettings serialization, or the Python HLA layer.

## Architecture

### Signal generation

`tdm_test_signal.h` provides `GenerateTdmSignal()`, which builds synthetic TDM signals as MockChannelData transitions. Signals use a counting pattern: slot values are 0, 1, 2, 3, ... wrapping at `2^data_bits_per_slot`. The `Config` struct controls all TDM parameters (frame rate, channel count, bit depth, alignment, shift order, DSP mode, etc.).

For error condition tests, hand-crafted signals bypass the generator entirely and place clock/data/frame transitions at exact sample positions.

### Two assertion layers

**V1 Frame (always available):** `MockResultData::GetFrame(i)` returns `mData1` (unsigned decoded value), `mType` (slot number), and `mFlags` (error bit flags). This is the primary assertion target for decode correctness.

**FrameV2 (via capture mock):** `extra_stubs.cpp` implements FrameV2 methods that store key/value pairs in `FrameV2Data`, and `AddFrameV2` copies them to a global vector. Tests call `ClearCapturedFrameV2s()` before running the analyzer and `GetCapturedFrameV2s()` after to inspect fields like `severity`, `data` (signed), `frame_number`, and error booleans. See `framev2_capture.h`.

Before the capture mock existed, all FrameV2 stubs were no-ops, making the entire FrameV2 layer (severity strings, signed conversion, error booleans, frame numbering) completely untestable. The capture mock was the single highest-impact improvement to the test suite.

### Test helpers

`tdm_test_helpers.h` provides shared infrastructure used by all test files:

- **CHECK / CHECK_EQ macros** - assert with descriptive failure messages, abort test on first failure
- **RunTest()** - runs a test function, tracks pass/fail counts
- **DecodedFrame** - struct wrapping V1 Frame fields (data, slot, flags)
- **RunAndCollect()** - generates signal from Config, runs analyzer, returns vector of DecodedFrame
- **VerifyCountingPattern()** - verifies decoded values match the counting pattern for a given Config
- **RunHandcraftedSignal()** - runs analyzer with manually constructed channel transitions
- **RunShortSlotTest() / RunExtraSlotsTest()** - shared error signal generators

## File Organization

| File | Tests | Purpose |
|------|------:|---------|
| `test_decode_values.cpp` | 28 | Value correctness: happy path (11), combination (6), boundary values (10), bit pattern (1) |
| `test_sign_conversion.cpp` | 9 | Sign conversion: unit tests (6), end-to-end (1), UB fix (1), FrameV2 signed decode (1) |
| `test_error_conditions.cpp` | 9 | Error flags (SHORT_SLOT, UNEXPECTED_BITS) and robustness under misconfig |
| `test_advanced_analysis.cpp` | 3 | Hand-crafted signals for BITCLOCK_ERROR, MISSED_DATA, MISSED_FRAME_SYNC |
| `test_generator_blindspots.cpp` | 3 | Padding bits HIGH, DSP Mode A offset bit, low sample rate |
| `test_framev2.cpp` | 7 | FrameV2 severity, error booleans, frame numbering, low SR advisory |
| `tdm_correctness.cpp` | - | Test runner: main() with forward declarations |
| **Total** | **58** | |

## Test Categories

### Happy path (11 tests) - `test_decode_values.cpp`

Each test varies one setting from the baseline (stereo 16-bit MSB-first left-aligned DSP Mode A) and verifies 100 frames of counting pattern data. Covers: MSB/LSB-first, left/right-aligned padding, 8-channel, DSP Mode B, frame sync inverted, negative edge sampling, 32-bit, 64-bit, mono.

**What they catch:** Any single-setting decode regression. A bug in the MSB-first loop, alignment indexing, or FS polarity detection would fail the corresponding test.

### Combination tests (6 tests) - `test_decode_values.cpp`

Vary multiple settings simultaneously. These catch interaction bugs that single-setting tests miss. For example, `test_combo_8ch_24in32_right_lsb` combines 8 channels, 24-in-32 right-aligned, and LSB-first - the starting_index skip and bit-weight assignment interact differently than either setting alone.

### Boundary values (10 tests) - `test_decode_values.cpp`

Test edge values for numeric parameters: non-power-of-2 bit widths (3-bit, 8-bit), extreme padding (2 data bits in 64-bit slot), 63-in-64 (1-bit padding), 256-slot U8 mType boundary, right-aligned with zero padding, and 8-bit counter wrapping through all 256 values to exercise high bit positions (0xFF, 0x80, 0xAA, 0x55) that the low-count counting pattern never reaches.

**Why the counter wrap test matters:** With 100 frames and 2 slots, the counter only reaches 199 for 16-bit data - less than 0.3% of the value space, never setting any bit above position 7. The 8-bit counter wrap test exercises all 256 values.

### Sign conversion (9 tests) - `test_sign_conversion.cpp`

Three levels: (1) Unit tests of `ConvertToSignedNumber` with known inputs including boundary conditions (0-bit, 1-bit, 64-bit). (2) End-to-end test with `sign = SignedInteger` enabled on the analyzer verifying unsigned mData1 is unaffected. (3) FrameV2 signed decode test verifying the `"data"` field contains correct signed values (e.g., 4-bit value 8 produces -8).

The FrameV2 test is critical - without it, the `ConvertToSignedNumber` call in `AnalyzeTdmSlot` could use the wrong bit width argument or be deleted entirely and no test would fail.

### Error conditions (9 tests) - `test_error_conditions.cpp`

Tests error flag detection (SHORT_SLOT flag with data=0 and post-error recovery, UNEXPECTED_BITS flag on excess slots) and robustness under misconfigured settings (fewer/more slots than expected, wrong bit depth, wrong DSP mode, wrong FS polarity). Error tests verify specific flags; misconfig tests verify no crash and check appropriate flags or valid output ranges.

### Advanced analysis (3 tests) - `test_advanced_analysis.cpp`

Hand-crafted signals with deliberate timing defects, using 8x oversampling (half-period = 4 samples) to provide room for placing transitions between clock edges:

- **BITCLOCK_ERROR:** One clock cycle stretched to 2x normal period. The analyzer checks `next_rising - current_rising` against `mDesiredBitClockPeriod +/- 1`.
- **MISSED_DATA:** Data line glitch (two extra transitions) between clock edges, plus a pending transition after the falling edge to satisfy the two-part detection condition.
- **MISSED_FRAME_SYNC:** Same pattern on the frame sync line.

### Generator blind spots (3 tests) - `test_generator_blindspots.cpp`

Address specific blind spots in the standard counting-pattern signal generator:

- **Padding bits HIGH:** The generator always fills padding with zeros, masking bugs where padding leaks into the decoded value. This test uses DSP Mode B with all-ones padding and verifies the analyzer decodes only the data bits.
- **DSP Mode A offset bit HIGH:** The first FS-coincident data bit is skipped during sync setup. This test sets it to HIGH and verifies it doesn't appear in decoded output.
- **Low sample rate:** Below 4x oversampling threshold, exercising the `mLowSampleRate` advisory path.

### FrameV2 field verification (7 tests) - `test_framev2.cpp`

Verify FrameV2 output fields that are invisible to V1 Frame inspection:

- **Happy path:** severity="ok", all error booleans=false, frame_number increments
- **Error severity:** SHORT_SLOT produces "error", EXTRA_SLOT produces "warning", BITCLOCK_ERROR/MISSED_DATA/MISSED_FRAME_SYNC produce "error"
- **Low sample rate:** Advisory FrameV2 emitted with severity="warning" and message, slot FrameV2s have low_sample_rate=true

## How to Add Tests

### Config-based test (most common)

1. Create a `Config` with `DefaultConfig()` and override the settings you want to test
2. Call `RunAndCollect(cfg)` to get decoded frames
3. Call `VerifyCountingPattern(frames, cfg, num_frames)` if testing value correctness
4. Or inspect `frames[i].data`, `frames[i].slot`, `frames[i].flags` directly
5. Add a `RunTest("test_name", test_function)` call in `tdm_correctness.cpp`
6. Add the function to the appropriate test file and forward-declare in `tdm_correctness.cpp`

### Hand-crafted signal test

1. Define transitions for clock, frame, and data channels as `vector<U64>`
2. Call `RunHandcraftedSignal()` with a `HandcraftedConfig`
3. Inspect the returned `DecodedFrame` vector
4. For FrameV2 inspection, call `ClearCapturedFrameV2s()` before and `GetCapturedFrameV2s()` after

### FrameV2 assertion test

1. Call `ClearCapturedFrameV2s()` before running the analyzer
2. Run via `RunAndCollect()` or a signal generator function
3. Iterate `GetCapturedFrameV2s()` and use `GetInteger()`, `GetString()`, `GetBoolean()` accessors
4. Filter by `fv2.type == "slot"` (data frames) or `fv2.type == "advisory"` (informational)

## Audit History

The test suite was built over three rounds, each driven by independent adversarial audit agents:

**Round 1 (20 tests):** Initial suite covering happy path, sign conversion unit tests, and basic error conditions. All tests used V1 Frame assertions only.

**Round 2 (+30 = 50 tests):** Three agents identified: counter only reaching 0-199, no non-power-of-2 widths, no LSB+padding combos, advanced analysis errors never triggered, misconfig tests too weak, padding always zero, signed path never tested end-to-end, ConvertToSignedNumber UB for 64-bit. All gaps addressed.

**Round 3 (+8 = 58 tests):** Three agents found the FrameV2 layer was the dominant blind spot - all FrameV2 stubs were no-ops, making signed conversion, severity, error booleans, and frame numbering completely untestable. Implemented FrameV2 capture mock and 8 verification tests. Also fixed fragile `test_padding_bits_high` signal construction and strengthened `test_256_slots` data assertions.

## Known Remaining Gaps

Per the latest audit, these areas are not covered by the correctness test suite:

- **GenerateBubbleText / GenerateTabularText / CSV / WAV export** (~500 lines of formatting logic in TdmAnalyzerResults.cpp)
- **Settings validation** (SetSettingsFromInterfaces: duplicate channels, data_bits > bits_per_slot, zero parameters, max bit clock)
- **LoadSettings / SaveSettings** serialization round-trip
- **SetupForGettingFirstBit** with clock starting HIGH (all tests start clock LOW)
- **The `<= 1` silent drop path** at AnalyzeTdmSlot line 289 (no test uses 1-bit data slots)
- **Visual markers** (channel assignments for arrows and stop markers) are not inspected
