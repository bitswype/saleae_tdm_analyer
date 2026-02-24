# Architecture

**Analysis Date:** 2026-02-23

## Pattern Overview

**Overall:** Plugin Architecture following Saleae Logic Analyzer SDK pattern

**Key Characteristics:**
- Protocol analyzer plugin for Saleae Logic 2 logic analyzer
- Pluggable architecture through DLL/SO shared library interface
- Stateful frame-based analysis with configurable parameters
- Three-phase processing: settings management, real-time analysis, and results export
- Supports multiple export formats (CSV, WAV)

## Layers

**Plugin Interface Layer:**
- Purpose: Provides the entry point for the Saleae Logic 2 application to load and create analyzer instances
- Location: `src/TdmAnalyzer.h` (lines 67-69, extern "C" functions)
- Contains: C-style export functions (`GetAnalyzerName`, `CreateAnalyzer`, `DestroyAnalyzer`)
- Depends on: Saleae Analyzer2 base class
- Used by: Logic 2 application runtime

**Analyzer Core (TdmAnalyzer):**
- Purpose: Main analysis engine that processes TDM serial data from logic analyzer capture
- Location: `src/TdmAnalyzer.cpp` and `src/TdmAnalyzer.h`
- Contains: Frame detection, bit extraction, slot analysis, error detection
- Depends on: `TdmAnalyzerSettings`, `TdmAnalyzerResults`, `TdmSimulationDataGenerator`
- Used by: Saleae SDK (`Analyzer2` parent class)

**Settings Management (TdmAnalyzerSettings):**
- Purpose: Stores and manages all configurable analysis parameters
- Location: `src/TdmAnalyzerSettings.cpp` and `src/TdmAnalyzerSettings.h`
- Contains: Channel selection, frame/slot/bit configuration, edge settings, alignment options, export format selection
- Depends on: Saleae SDK analyzer interfaces
- Used by: `TdmAnalyzer` for analysis behavior, `TdmAnalyzerResults` for export behavior

**Results/Export Layer (TdmAnalyzerResults):**
- Purpose: Generates protocol display bubbles and exports decoded data in multiple formats
- Location: `src/TdmAnalyzerResults.cpp` and `src/TdmAnalyzerResults.h`
- Contains: Bubble text generation, CSV export, WAV file generation with PCM headers
- Depends on: `TdmAnalyzerSettings`, WAV header structures, file I/O
- Used by: Logic 2 UI for display and export operations

**Simulation/Test Data Layer (TdmSimulationDataGenerator):**
- Purpose: Generates synthetic TDM test data for testing without real hardware
- Location: `src/TdmSimulationDataGenerator.cpp` and `src/TdmSimulationDataGenerator.h`
- Contains: Sine wave generator, counter generator, static value generator, clock generator, frame/bit generation
- Depends on: `TdmAnalyzerSettings`, math library
- Used by: `TdmAnalyzer` for test data generation

## Data Flow

**Real-time Analysis Flow:**

1. Logic 2 calls analyzer entry point → `CreateAnalyzer()`
2. User configures settings via `TdmAnalyzerSettings` UI interfaces
3. Logic 2 calls `WorkerThread()` with captured digital data
4. `WorkerThread()` initializes analysis:
   - Sets up clock/frame/data channel readers from `AnalyzerChannelData`
   - Calls `SetupForGettingFirstBit()` to synchronize to correct clock edge
   - Calls `SetupForGettingFirstTdmFrame()` to find first frame sync pulse
5. Main analysis loop in `GetTdmFrame()`:
   - Detects frame sync transitions (rising or falling edge based on settings)
   - Collects bits from data channel using `GetNextBit()` at each clock edge
   - When enough bits collected for a slot, calls `AnalyzeTdmSlot()`
   - Continues until next frame sync detected
6. `AnalyzeTdmSlot()` processes accumulated bits:
   - Checks for errors (short slot, extra slots, data errors, frame sync errors, bitclock errors)
   - Extracts data bits respecting alignment (left/right), bit order (MSB/LSB first)
   - Creates Frame with slot number, data value, and error flags
   - Adds frame to results via `mResults->AddFrame()`
7. `TdmAnalyzerResults` generates display bubbles and handles exports

**Export Flow:**

1. User selects "Export to TXT/CSV" in Logic 2
2. `GenerateExportFile()` called with selected file path
3. Checks `mSettings->mExportFileType` (CSV or WAV)
4. If WAV: `GenerateWAV()` → `PCMWaveFileHandler` writes binary PCM data with proper header
5. If CSV: `GenerateCSV()` writes tabular text format with all data, flags, timing

**State Management:**
- Settings: Stored in `TdmAnalyzerSettings` member variables, UI state synchronized via `UpdateInterfacesFromSettings()`
- Analysis state: Maintained across bit/frame processing (mCurrentDataState, mCurrentFrameState, mCurrentSample, mDataBits vector)
- Results: Accumulated in `TdmAnalyzerResults` frame list, generated on-demand for display/export
- Simulation: Maintained in `TdmSimulationDataGenerator` (sine generators, frame bits, current position)

## Key Abstractions

**Frame Concept:**
- Purpose: Represents one decoded TDM slot with its data value and metadata
- Examples: `src/TdmAnalyzer.h` line 39 (Frame mResultsFrame), `src/TdmAnalyzer.cpp` line 214-295 (AnalyzeTdmSlot)
- Pattern: Saleae SDK `Frame` struct containing type (slot number), data (slot value), flags (errors/warnings), sample range

**Bit Collection:**
- Purpose: Accumulates raw bits from data channel across multiple clock edges
- Examples: `src/TdmAnalyzer.h` lines 61-63 (mDataBits, mDataValidEdges, mDataFlags vectors)
- Pattern: Three parallel vectors tracking bit values, sample numbers where bits captured, and per-bit flags

**Error Flagging:**
- Purpose: Mark analysis issues with configurable flags that display in UI and export
- Examples: `src/TdmAnalyzerResults.h` lines 9-15 (MISSED_DATA, SHORT_SLOT, BITCLOCK_ERROR flags)
- Pattern: Bitfield approach with separate flags for each error type plus summary ERROR/WARNING flags

**WAV File Handling:**
- Purpose: Write TDM slot data as PCM audio with multi-channel WAV format
- Examples: `src/TdmAnalyzerResults.h` lines 96-159 (PCMWaveFileHandler, PCMExtendedWaveFileHandler classes)
- Pattern: Header struct with PCM/extended PCM formats, buffered sample writing with periodic header updates

## Entry Points

**Plugin Creation:**
- Location: `src/TdmAnalyzer.h` line 67 - `extern "C" Analyzer* __cdecl CreateAnalyzer()`
- Triggers: Logic 2 loads analyzer plugin on startup
- Responsibilities: Allocates new `TdmAnalyzer` instance, returns pointer for SDK to manage

**WorkerThread:**
- Location: `src/TdmAnalyzer.h` line 15 - `virtual void WorkerThread()`
- Triggers: Logic 2 starts analysis after user clicks "Start" with captured data
- Responsibilities: Main analysis loop processing all captured samples

**Simulation Data Generation:**
- Location: `src/TdmAnalyzer.h` line 17 - `virtual U32 GenerateSimulationData(...)`
- Triggers: Logic 2 when user selects analyzer without real capture or for testing
- Responsibilities: Generates synthetic TDM signal data for testing

## Error Handling

**Strategy:** Flag-based error detection with dual-layer reporting (frame flags + markers)

**Patterns:**
- Short Slot (0x08): Detected in `AnalyzeTdmSlot()` when fewer bits collected than expected (line 233-236 in TdmAnalyzer.cpp)
- Data Error (0x04, advanced only): Detected in `GetNextBit()` when data transitions between clock edges (line 200-204 in TdmAnalyzer.cpp)
- Frame Sync Missed (0x10, advanced only): Detected in `GetNextBit()` when frame sync transitions between clock edges (line 206-210 in TdmAnalyzer.cpp)
- Bitclock Error (0x20, advanced only): Detected in `GetNextBit()` when clock period deviates from expected by >1 sample (comparison with mDesiredBitClockPeriod)
- Extra Slot (0x02): Detected in `AnalyzeTdmSlot()` when slot number exceeds configured slots per frame (line 228-231 in TdmAnalyzer.cpp)

Markers are added to signal lines showing exact sample positions of errors for visual inspection.

## Cross-Cutting Concerns

**Logging:** None - protocol analyzer has no logging infrastructure. Errors reported via frame flags and display bubbles.

**Validation:**
- Settings validated via `SetSettingsFromInterfaces()` in `TdmAnalyzerSettings` (ensures channels selected, parameter ranges valid)
- Bit collection validated during `GetNextBit()` with advanced analysis checks

**Authentication:** Not applicable - plugin-based analyzer, no user authentication needed.

**Channel Configuration:**
- Three channels configured: Clock (bit clock), Frame (frame sync), Data (serial data)
- Channel selection stored in `mClockChannel`, `mFrameChannel`, `mDataChannel` of `TdmAnalyzerSettings`
- Converted to live `AnalyzerChannelData` objects in `WorkerThread()` for real-time reading

---

*Architecture analysis: 2026-02-23*
