# Testing Patterns

**Analysis Date:** 2026-02-23

## Test Framework

**Status:** No unit test framework detected

This is a Saleae Logic Analyzer plugin written in C++ that compiles to a shared library (.so on Linux, .dll on Windows, .dylib on macOS). The plugin architecture does not include a traditional unit test suite. Testing is performed through:
1. Manual integration testing via the Logic 2 analyzer application
2. Simulation data generation for validation
3. Export file verification (CSV, WAV)

**Build System:**
- CMake 3.11+ (configured in `/home/chris/gitrepos/saleae_tdm_analyer/CMakeLists.txt`)
- Saleae AnalyzerSDK (external module loaded via `ExternalAnalyzerSDK.cmake`)
- Cross-platform: Windows (MSVC), macOS (XCode/Clang), Linux (GCC)

**Run Commands:**
```bash
# Linux/macOS
mkdir build
cd build
cmake ..
cmake --build .

# Linux release build
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

# Linux debug build
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Windows
mkdir build
cd build
cmake .. -A x64
# Then open build\tdm_analyzer.sln in Visual Studio
```

## Test File Organization

**Location:**
- No test files found in the codebase
- Directory structure has no `test/`, `tests/`, `spec/`, or `__tests__` directories

**Simulation Testing:**
- `TdmSimulationDataGenerator.h` and `TdmSimulationDataGenerator.cpp` in `/home/chris/gitrepos/saleae_tdm_analyer/src/`
  - Generates synthetic TDM data for testing analyzer behavior
  - Used internally by the analyzer to validate capture and decode logic
  - Supports multiple data generation patterns: sine wave, counter, static values

## Manual Testing Approach

**Integration Testing:**
The analyzer is tested through manual integration with the Saleae Logic 2 application:

1. **Add analyzer to Logic 2**
   - Load the compiled plugin `.so`/`.dll`/`.dylib` into Logic 2
   - Configure TDM parameters (slots/frame, bits/slot, etc.)

2. **Generate simulated data**
   - Use the built-in simulation feature to create synthetic TDM frames
   - Verify frame capture and slot alignment

3. **Validate outputs**
   - Check decoded data bubbles in the protocol table
   - Export to CSV and verify frame/slot data matches expected values
   - Export to WAV and verify audio channels and sample rates

4. **Error condition testing**
   - Configure mismatched settings to trigger expected errors:
     - Short slot detection (`SHORT_SLOT` flag)
     - Unexpected bits (`UNEXPECTED_BITS` flag)
     - Bitclock errors (`BITCLOCK_ERROR` flag)
     - Data transitions not captured (`MISSED_DATA` flag)
     - Frame sync loss (`MISSED_FRAME_SYNC` flag)

## Test Structure

**Simulation Data Generation Pattern:**

The `TdmSimulationDataGenerator` class demonstrates the data generation approach:

```cpp
// From TdmSimulationDataGenerator.h
class SineGen
{
  public:
  SineGen(double sample_rate = 1000.0, double frequency_hz = 1.0, double scale = 1.0, double phase_degrees = 0.0);
  double GetNextValue();
  void Reset();
  // ...
};

class CountGen
{
  public:
  CountGen(U64 start_val, U64 max_val);
  U64 GetNextValue();
  void Reset();
  // ...
};

class StaticGen
{
  public:
  StaticGen(U64 val);
  U64 GetNextValue();
  void Reset();
  // ...
};
```

**Data Generation States:**
```cpp
// From TdmSimulationDataGenerator.h
enum BitGenerationState
{
    Init,
    LeftPadding,
    Data,
    RightPadding
};
```

The simulation generates TDM frames with configurable:
- Audio sample generators (sine wave, counter, static)
- Bit alignment (left/right justified)
- Frame sync patterns
- Bit ordering (MSB/LSB first)

## Validation Approach

**Frame Validation:**

The analyzer validates incoming data against expected TDM protocol:

```cpp
// From TdmAnalyzer.cpp - Frame analysis
void TdmAnalyzer::AnalyzeTdmSlot()
{
    U64 result = 0;
    U32 starting_index = 0;
    size_t num_bits_to_process = mDataBits.size();

    // Validate slot count
    if( mSlotNum >= mSettings->mSlotsPerFrame )
    {
        mResultsFrame.mFlags |= UNEXPECTED_BITS | DISPLAY_AS_WARNING_FLAG;
    }

    // Validate slot size
    if( num_bits_to_process < mSettings->mBitsPerSlot )
    {
        mResultsFrame.mFlags |= SHORT_SLOT | DISPLAY_AS_ERROR_FLAG;
    }

    // ... process data ...
}
```

**Advanced Analysis Validation (optional):**

When `mEnableAdvancedAnalysis == true`, additional validation occurs:

```cpp
// From TdmAnalyzer.cpp - GetNextBit()
// Check for bitclock period deviation
if(((next_clk_edge_sample - data_valid_sample) > (U64(mDesiredBitClockPeriod) + 1)) ||
   ((next_clk_edge_sample - data_valid_sample) < (U64(mDesiredBitClockPeriod) - 1)))
{
    mBitFlag |= BITCLOCK_ERROR | DISPLAY_AS_ERROR_FLAG;
}

// Check for data transitions between clock edges
if((data_tranistions > 0) && ( mData->WouldAdvancingToAbsPositionCauseTransition( next_clk_edge_sample ) == true))
{
    mResults->AddMarker(next_clock_edge, AnalyzerResults::MarkerType::Stop, mSettings->mDataChannel);
    mBitFlag |= MISSED_DATA | DISPLAY_AS_ERROR_FLAG;
}

// Check for frame sync transitions between clock edges
if((frame_transitions > 0) && ( mFrame->WouldAdvancingToAbsPositionCauseTransition( next_clk_edge_sample ) == true))
{
    mResults->AddMarker(next_clock_edge, AnalyzerResults::MarkerType::Stop, mSettings->mFrameChannel);
    mBitFlag |= MISSED_FRAME_SYNC | DISPLAY_AS_ERROR_FLAG;
}
```

## Error Detection Flags

**Bit Flags (from `TdmAnalyzerResults.h`):**
```cpp
#define UNEXPECTED_BITS         ( 1 << 1 ) // 0x02 - Extra slots detected
#define MISSED_DATA             ( 1 << 2 ) // 0x04 - Data changed between clock edges
#define SHORT_SLOT              ( 1 << 3 ) // 0x08 - Slot has fewer bits than expected
#define MISSED_FRAME_SYNC       ( 1 << 4 ) // 0x10 - Frame sync changed between clock edges
#define BITCLOCK_ERROR          ( 1 << 5 ) // 0x20 - Clock frequency deviation detected
// bits 6 & 7 reserved for warning / error display flags
```

**Display Flags:**
```cpp
#define DISPLAY_AS_WARNING_FLAG ( 1 << 6 ) // 0x40
#define DISPLAY_AS_ERROR_FLAG   ( 1 << 7 ) // 0x80
```

## Export File Testing

**CSV Export:**
- Exports frame data with flags, channel numbers, and decoded values
- Format: frame data, flags, channel, sample timing
- Used to verify correct slot decoding and error flagging

**WAV Export:**
- Generates PCM wave file with multi-channel audio
- Sample rate: configurable from analyzer settings
- Channel count: matches number of slots per frame
- Bit depth: auto-selected based on data bits (8, 16, 32, 40, 48, 64)
- PCM header: standard (supports up to 256 channels and 32-bit depth)

**WAV File Handler Pattern:**

```cpp
// From TdmAnalyzerResults.h
class PCMWaveFileHandler
{
  public:
    PCMWaveFileHandler(std::ofstream & file, U32 sample_rate = 48000, U32 num_channels = 2, U32 bits_per_channel = 32);
    void addSample(U64 sample);
    void close(void);
  private:
    void writeLittleEndianData(U64 value, U8 num_bytes);
    void updateFileSize();
    // ... internal state ...
};
```

## Coverage

**Test Coverage:** Not applicable
- No unit test framework means no code coverage metrics
- Manual integration testing covers:
  - Normal frame capture and decode
  - Error condition detection
  - Export functionality (CSV, WAV)
  - Configuration validation
  - Edge cases (empty frames, misaligned data, clock anomalies)

**Areas Tested Manually:**
- Frame sync detection (rising/falling edge sensitivity)
- Bit alignment (left/right justified, shifted vs. non-shifted)
- Slot/frame organization (1-256 slots, 2-64 bits/slot)
- Data extraction (MSB/LSB first)
- Advanced analysis features (bitclock, data transitions, frame sync detection)

## Testing Commands

```bash
# Build for testing (Linux debug)
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Build for testing (Linux release with symbols)
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

# Manual testing (Linux with GDB)
# 1. Identify Logic process: ps aux | grep 'Logic'
# 2. Attach debugger: gdb ./build-debug/libtdm_analyzer.so
# 3. Attach to process: attach <pid>
# 4. Set breakpoint: break TdmAnalyzer::WorkerThread
# 5. Continue and step through: continue, step, next
```

## Known Testing Limitations

1. **No automated unit tests** - Plugin architecture requires Logic 2 application to load and test
2. **Manual export validation** - CSV and WAV files must be manually verified
3. **Limited simulation data** - Simulation generator only supports simple patterns (sine, counter)
4. **No CI/CD test runner** - Azure Pipelines builds but does not run automated tests
5. **Platform-specific issues** - Debugging on Linux requires special process handling due to AppImage wrapper

---

*Testing analysis: 2026-02-23*
