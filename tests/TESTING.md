# TDM Analyzer Test Suite

## Overview

Two test suites cover the TDM analyzer decode pipeline:

**C++ LLA correctness tests** (`tests/tdm_correctness`) -- 79 tests verifying the Low Level Analyzer's decode logic via the SDK testlib. See detailed documentation below.

**Python HLA decode tests** (`tests/test_hla_decode.py`) -- 74 pytest tests verifying both HLAs (audio stream and WAV export) at the unit level. Covers every branch of decode(): frame/slot filtering, sign conversion, error handling, frame boundary detection, accumulator behavior, PCM packing via TCP, batch buffer mechanics, sample rate derivation, WAV output, and C-port safety (negative ints, overflow, missing keys, ring buffer overflow, large frame numbers). Run with `pytest tests/test_hla_decode.py -v`. This suite serves as the correctness oracle for any future C/Cython reimplementation of the decode() hot path.

**What is not tested:** GenerateBubbleText/GenerateTabularText (formatting logic), CSV/WAV export format details, settings validation (SetSettingsFromInterfaces), LoadSettings/SaveSettings serialization.

## Building and Running

### C++ LLA tests

## Building and Running

```bash
# Configure (first time or after CMakeLists.txt changes)
cmake -B build-test -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release    # Linux/macOS
cmake -B build-test -DBUILD_TESTS=ON -A x64                         # Windows

# Build
cmake --build build-test --target tdm_correctness                    # Linux/macOS
cmake --build build-test --config Release --target tdm_correctness   # Windows

# Run (exit code 0 = all pass, 1 = any failure)
./build-test/bin/tdm_correctness                                      # Linux/macOS
build-test\bin\Release\tdm_correctness.exe                           # Windows
```

### Python HLA tests

```bash
pip install pytest
pip install -e tools/tdm-test-harness/
pytest tests/test_hla_decode.py -v
```

## TDM Protocol Primer

TDM (Time Division Multiplexing) is a serial audio protocol with three signal lines:

- **Bit clock:** Square wave that defines when data bits are valid. Data is sampled on either the rising edge (PosEdge) or falling edge (NegEdge).
- **Frame sync (FS):** A pulse that marks the start of a new TDM frame. Each frame contains N time slots, one per audio channel. The pulse is one clock cycle wide. FS can be active-HIGH (normal) or active-LOW (inverted).
- **Data:** Serial audio sample bits, transmitted MSB-first or LSB-first.

**DSP Mode A vs B:** In DSP Mode A (most common), data is delayed by one clock cycle relative to the FS pulse - the bit at the FS edge is the last bit of the previous frame, and the new frame's data starts on the next clock cycle. In DSP Mode B, data starts on the same clock cycle as the FS pulse (no delay).

**Oversampling:** The Logic 2 capture sample rate must be at least 4x the bit clock frequency for reliable edge detection. The bit clock frequency is `frame_rate * slots_per_frame * bits_per_slot`.

**Preamble:** The signal generator prepends 16 idle clock cycles before the first FS pulse so the analyzer can synchronize before the first data arrives.

### Error flag constants

Defined in `src/TdmAnalyzerResults.h`, masked by `ERROR_FLAG_MASK` (0x3F) in tests:

| Flag | Bit | Value | Meaning |
|------|-----|-------|---------|
| `TOO_FEW_BITS_PER_FRAME` | 0 | 0x01 | (Unused) |
| `UNEXPECTED_BITS` | 1 | 0x02 | More slots in frame than configured |
| `MISSED_DATA` | 2 | 0x04 | Data line transitioned between clock edges (advanced analysis) |
| `SHORT_SLOT` | 3 | 0x08 | Fewer bits in slot than configured |
| `MISSED_FRAME_SYNC` | 4 | 0x10 | Frame sync transitioned between clock edges (advanced analysis) |
| `BITCLOCK_ERROR` | 5 | 0x20 | Clock period deviates from expected by more than +/-1 sample (advanced analysis) |

Bits 6-7 are display flags (DISPLAY_AS_WARNING_FLAG, DISPLAY_AS_ERROR_FLAG) used by Logic 2's UI. Tests mask these out with `ERROR_FLAG_MASK`.

## Architecture

### Signal generation

`tdm_test_signal.h` provides `GenerateTdmSignal()`, which builds synthetic TDM signals as MockChannelData transitions. Signals use a counting pattern: each slot value increments sequentially across all slots (not per-slot) wrapping at `2^data_bits_per_slot`. For example, with 2 slots: frame 0 = [0, 1], frame 1 = [2, 3], frame 2 = [4, 5], etc. The `Config` struct controls all TDM parameters.

For error condition and blind spot tests, hand-crafted signals bypass the generator entirely and place clock/data/frame transitions at exact sample positions using `EmitBitDescSignal()` or `RunHandcraftedSignal()`.

### Two assertion layers

**V1 Frame (always available):** `MockResultData::GetFrame(i)` returns `mData1` (unsigned decoded value), `mType` (slot number), and `mFlags` (error bit flags). This is the primary assertion target for decode correctness.

**FrameV2 (via capture mock):** `extra_stubs.cpp` implements FrameV2 methods that store key/value pairs in `FrameV2Data`, and `AddFrameV2` copies them to a global vector. Tests call `ClearCapturedFrameV2s()` before running the analyzer and `GetCapturedFrameV2s()` after to inspect fields like `severity`, `data` (signed), `frame_number`, and error booleans. See `framev2_capture.h`.

Before the capture mock existed, all FrameV2 stubs were no-ops, making the entire FrameV2 layer (severity strings, signed conversion, error booleans, frame numbering) completely untestable. The capture mock was the single highest-impact improvement to the test suite.

### Test helpers

`tdm_test_helpers.h` declares shared infrastructure; implementations live in `tdm_test_helpers.cpp`:

- **CHECK / CHECK_EQ macros** - assert with descriptive failure messages, abort test on first failure
- **RunTest()** - runs a test function, tracks pass/fail counts
- **DecodedFrame** - struct wrapping V1 Frame fields (data, slot, flags)
- **ERROR_FLAG_MASK** - named constant (0x3F) for masking V1 Frame error bits
- **BitDesc** - struct for per-bit signal description (data state, frame state)
- **RunAndCollect()** - generates signal from Config, runs analyzer, returns vector of DecodedFrame
- **VerifyCountingPattern()** - verifies decoded values match the counting pattern
- **EmitBitDescSignal()** - converts a BitDesc vector into MockChannelData transitions
- **RunHandcraftedSignal()** - runs analyzer with manually constructed transition lists. NOTE: hardcodes MSB-first, PosEdge, LEFT_ALIGNED, DSP_MODE_A, UnsignedInteger, FS_NOT_INVERTED. Callers needing other settings must build their own Instance setup.
- **RunShortSlotTest() / RunExtraSlotsTest()** - error signal generators for SHORT_SLOT and UNEXPECTED_BITS
- **RunBitclockErrorSignal() / RunMissedDataSignal() / RunMissedFrameSyncSignal()** - error signal generators for advanced analysis flags
- **RunWithMismatch()** - generates signal from one Config but configures analyzer from another (for misconfig tests)

## File Organization

| File | Tests | Purpose |
|------|------:|---------|
| `test_decode_values.cpp` | 27 | Value correctness: happy path (11), combination (6), boundary values (9), bit pattern (1) |
| `test_sign_conversion.cpp` | 9 | Sign conversion: unit tests (6), end-to-end (1), UB fix (1), FrameV2 signed decode (1) |
| `test_error_conditions.cpp` | 9 | Error flags (SHORT_SLOT, UNEXPECTED_BITS) and robustness under misconfig |
| `test_advanced_analysis.cpp` | 3 | Hand-crafted signals for BITCLOCK_ERROR, MISSED_DATA, MISSED_FRAME_SYNC |
| `test_generator_blindspots.cpp` | 3 | Padding bits HIGH, DSP Mode A offset bit, low sample rate |
| `test_framev2.cpp` | 7 | FrameV2 severity, error booleans, frame numbering, low SR advisory |
| `test_audio_batch.cpp` | 21 | Audio batch mode: happy path (8), PCM oracle (4), multi-channel/bit-depth (4), edge cases (3), error handling (2) |
| `tdm_test_helpers.h` | - | Declarations and macros (inline RunTest, CHECK) |
| `tdm_test_helpers.cpp` | - | Helper implementations and shared signal generators |
| `tdm_correctness.cpp` | - | Test runner: main() with forward declarations |
| **Total** | **79** | |

## Test Categories

### Happy path (11 tests) - `test_decode_values.cpp`

Each test varies one setting from the baseline (stereo 16-bit MSB-first left-aligned DSP Mode A) and verifies 100 frames of counting pattern data. Covers: MSB/LSB-first, left/right-aligned padding, 8-channel, DSP Mode B, frame sync inverted, negative edge sampling, 32-bit, 64-bit, mono.

**What they catch:** Any single-setting decode regression. A bug in the MSB-first loop, alignment indexing, or FS polarity detection would fail the corresponding test.

### Combination tests (6 tests) - `test_decode_values.cpp`

Vary multiple settings simultaneously. These catch interaction bugs that single-setting tests miss. For example, `test_combo_8ch_24in32_right_lsb` combines 8 channels, 24-in-32 right-aligned, and LSB-first - the starting_index skip and bit-weight assignment interact differently than either setting alone.

### Boundary values (9 tests) - `test_decode_values.cpp`

Test edge values for numeric parameters: non-power-of-2 bit widths (3-bit, 8-bit), extreme padding (2 data bits in 64-bit slot), 63-in-64 (1-bit padding), 256-slot U8 mType boundary, and right-aligned with zero padding.

### Bit pattern coverage (1 test) - `test_decode_values.cpp`

8-bit counter wrapping through all 256 values (0x00-0xFF), exercising high bit positions (0xFF, 0x80, 0xAA, 0x55) that the low-count counting pattern never reaches.

**Why this matters:** With 100 frames and 2 slots, the counter only reaches 199 for 16-bit data - less than 0.3% of the value space, never setting any bit above position 7.

### Sign conversion (9 tests) - `test_sign_conversion.cpp`

Three levels: (1) Unit tests of `ConvertToSignedNumber` with known inputs including boundary conditions (0-bit, 1-bit, 64-bit). (2) End-to-end test with `sign = SignedInteger` enabled on the analyzer verifying unsigned mData1 is unaffected. (3) FrameV2 signed decode test verifying the `"data"` field contains correct signed values (e.g., 4-bit value 8 produces -8).

The FrameV2 test is critical - without it, the `ConvertToSignedNumber` call in `AnalyzeTdmSlot` could use the wrong bit width argument or be deleted entirely and no test would fail.

### Error conditions (9 tests) - `test_error_conditions.cpp`

Tests error flag detection (SHORT_SLOT flag with data=0 and post-error recovery, UNEXPECTED_BITS flag on excess slots) and robustness under misconfigured settings (fewer/more slots than expected, wrong bit depth, wrong DSP mode, wrong FS polarity). Error tests verify specific flags; misconfig tests use `RunWithMismatch()` and verify no crash plus appropriate flags.

### Advanced analysis (3 tests) - `test_advanced_analysis.cpp`

Hand-crafted signals with deliberate timing defects, using 8x oversampling (half-period = 4 samples) to provide room for placing transitions between clock edges:

- **BITCLOCK_ERROR:** One clock cycle stretched to 2x normal period (16 samples vs expected 8). The analyzer checks `next_rising - current_rising` against `mDesiredBitClockPeriod +/- 1`.
- **MISSED_DATA:** Data line glitch (two extra transitions) between clock edges, plus a pending transition after the falling edge. The analyzer requires both conditions: transitions in the first half-period AND a pending transition before the next rising edge.
- **MISSED_FRAME_SYNC:** Same two-part pattern on the frame sync line.

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

Note: `test_framev2_signed_decode` (1 test) lives in `test_sign_conversion.cpp` since it verifies signed conversion correctness, and runs under the "FrameV2 Field Verification" section in the runner.

## How to Add Tests

### DefaultConfig baseline

`DefaultConfig(label, num_frames)` produces: stereo (2 slots), 16-bit, 48 kHz, MSB-first, left-aligned, DSP Mode A, unsigned, PosEdge, FS not inverted, 4x oversampling.

**Sample rate formula:** When you change `slots_per_frame`, `bits_per_slot`, or `frame_rate`, you must recalculate:

```
sample_rate = frame_rate * slots_per_frame * bits_per_slot * oversampling
```

Use 4x oversampling for standard tests. Use 8x for hand-crafted advanced analysis tests (more room between clock edges).

### Config-based test (most common)

1. Create a `Config` with `DefaultConfig()` and override the settings you want to test
2. Recalculate `c.sample_rate` if you changed frame_rate, slots_per_frame, or bits_per_slot
3. Call `RunAndCollect(cfg)` to get decoded frames
4. Call `VerifyCountingPattern(frames, cfg, num_frames)` if testing value correctness
5. Or inspect `frames[i].data`, `frames[i].slot`, `frames[i].flags` directly
6. Add the function to the appropriate test `.cpp` file
7. Add a forward declaration and `RunTest()` call in `tdm_correctness.cpp` under the matching category

### Hand-crafted signal test

For tests that need non-standard signal construction (error injection, non-zero padding, etc.):

1. Build a `vector<BitDesc>` describing each clock cycle's data and frame state
2. Call `EmitBitDescSignal(clk, frm, dat, stream, half_samples)` to convert to MockChannelData
3. Or use `RunHandcraftedSignal()` for simpler cases (note: it hardcodes several settings)
4. If adding a reusable signal generator, put it in `tdm_test_helpers.cpp` with a declaration in `.h`

### FrameV2 assertion test

1. Call `ClearCapturedFrameV2s()` before running the analyzer (any prior test's capture data persists in the global buffer otherwise)
2. Run via `RunAndCollect()` or a signal generator function
3. Iterate `GetCapturedFrameV2s()` and use `GetInteger()`, `GetString()`, `GetBoolean()` accessors
4. Filter by `fv2.type == "slot"` (data frames) or `fv2.type == "advisory"` (informational)

## Audit History

The test suite was built over three rounds, each driven by independent adversarial audit agents:

**Round 1 (20 tests):** Initial suite covering happy path, sign conversion unit tests, and basic error conditions. All tests used V1 Frame assertions only.

**Round 2 (+30 = 50 tests):** Three agents identified: counter only reaching 0-199, no non-power-of-2 widths, no LSB+padding combos, advanced analysis errors never triggered, misconfig tests too weak, padding always zero, signed path never tested end-to-end, ConvertToSignedNumber UB for 64-bit. All gaps addressed.

**Round 3 (+8 = 58 tests):** Three agents found the FrameV2 layer was the dominant blind spot - all FrameV2 stubs were no-ops, making signed conversion, severity, error booleans, and frame numbering completely untestable. Implemented FrameV2 capture mock and verification tests. Also fixed fragile `test_padding_bits_high` signal construction and strengthened `test_256_slots` data assertions.

**Round 4 (cleanup):** Three more agents audited the split and documentation. Fixes: extracted signal generators to eliminate cross-file coupling, moved large functions from header to .cpp, deduplicated BitDesc conversion and misconfig boilerplate, added ERROR_FLAG_MASK constant, fixed runner category groupings, removed dead code (RunWithoutCrash), and added this documentation.

**Round 5 (+21 = 79 tests):** Audio batch mode tests added for v2.5.0: batch emission, PCM byte-level oracle verification, multi-channel and 32-bit depth, edge cases (batch=1, partial flush), and error handling (batch with error frames).

**HLA Round 1 (42 tests):** Initial Python HLA decode() unit test suite covering frame/slot filtering, sign conversion, error handling, frame boundaries, accumulator behavior, PCM packing via TCP, batch mechanics, sample rate derivation, init errors, and WAV export.

**HLA Round 2 (+22 = 64 tests, +10 batch = 74 total):** Three adversarial agents found: hollow tests (silence test checked count not PCM, shutdown test called flush manually), missing C port safety (negative ints, overflow, missing keys, ring overflow, large frame_nums, zero delta), weak tests (error substitution unverified, batch flush not ring-verified), missing WAV edge cases, and missing parse_slot_spec edge cases. All addressed.

## Known Remaining Gaps

Per the latest audits, these areas are not covered:

### C++ LLA
- **GenerateBubbleText / GenerateTabularText / CSV / WAV export** (~500 lines of formatting logic in TdmAnalyzerResults.cpp)
- **Settings validation** (SetSettingsFromInterfaces: duplicate channels, data_bits > bits_per_slot, zero parameters, max bit clock)
- **LoadSettings / SaveSettings** serialization round-trip
- **SetupForGettingFirstBit** with clock starting HIGH (all tests start clock LOW)
- **The `<= 1` silent drop path** at AnalyzeTdmSlot line 289 (no test uses 1-bit data slots)
- **Visual markers** (channel assignments for arrows and stop markers) are not inspected

### Python HLA
- **Sender thread error handling** (client disconnect mid-drain, sendall failure)
- **Shutdown stopping message** (JSON `{"type":"stopping"}` sent to client)
- **Concurrent producer/consumer stress** (high-throughput ring buffer under active sender thread)
- **WAV file open failure** (unwritable path propagates exception from decode)
- **Deferred handshake timing** (client connects before sample rate known, handshake sent later)
