# Architecture Research

**Domain:** Saleae Logic 2 protocol analyzer plugin (Low-Level Analyzer, C++ SDK)
**Researched:** 2026-02-23
**Confidence:** HIGH (official SDK source, official Saleae analyzers, Saleae support docs)

## Standard Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Logic 2 Application                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │  Analyzer UI  │  │  Data Table  │  │  Export (.txt/.csv)  │   │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘   │
│         │                 │                      │               │
│         │   SDK boundary (shared library call)   │               │
├─────────┴─────────────────┴──────────────────────┴───────────────┤
│               Plugin Shared Library (.so / .dll)                  │
│                                                                   │
│  extern "C" entry points                                          │
│  ┌──────────────┐  ┌─────────────────┐  ┌────────────────────┐   │
│  │ GetAnalyzer  │  │ CreateAnalyzer  │  │ DestroyAnalyzer    │   │
│  │ Name()       │  │ ()              │  │ (Analyzer*)        │   │
│  └──────────────┘  └────────┬────────┘  └────────────────────┘   │
│                             │                                     │
│  ┌──────────────────────────▼──────────────────────────────────┐  │
│  │                   XyzAnalyzer : Analyzer2                    │  │
│  │  SetupResults()   WorkerThread()   GenerateSimulationData()  │  │
│  └─────────┬────────────────────────────┬───────────────────────┘  │
│            │                            │                          │
│  ┌─────────▼───────────┐   ┌────────────▼──────────────────────┐  │
│  │  XyzAnalyzerSettings│   │  XyzAnalyzerResults               │  │
│  │  : AnalyzerSettings │   │  : AnalyzerResults                │  │
│  │                     │   │                                   │  │
│  │  UI interfaces      │   │  GenerateBubbleText()             │  │
│  │  Serialization      │   │  GenerateExportFile()             │  │
│  │  Channel config     │   │  GenerateFrameTabularText()       │  │
│  └─────────────────────┘   │  GeneratePacketTabularText()      │  │
│                            │  GenerateTransactionTabularText() │  │
│                            └───────────────────────────────────┘  │
│                                                                   │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │  XyzSimulationDataGenerator                                 │   │
│  │  InitializeSimulationData()   GenerateSimulationData()      │   │
│  └────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | SDK Base Class |
|-----------|----------------|----------------|
| XyzAnalyzer | Core analysis loop (WorkerThread), channel data reading, frame creation, progress reporting | `Analyzer2` |
| XyzAnalyzerSettings | User-configurable parameters, channel selection, settings serialization, export option registration | `AnalyzerSettings` |
| XyzAnalyzerResults | Bubble text display, tabular data display, file export (CSV/WAV/etc.) | `AnalyzerResults` |
| XyzSimulationDataGenerator | Synthetic signal generation for testing without hardware | (standalone, instantiated by Analyzer) |
| extern "C" entry points | DLL/SO interface for Logic 2 to create/destroy analyzer instances | (free functions) |

## Recommended Project Structure

The Saleae SampleAnalyzer and all official Saleae analyzers follow this four-file-pair structure:

```
src/
├── XyzAnalyzer.h / .cpp              # Core: Analyzer2 subclass, WorkerThread
├── XyzAnalyzerSettings.h / .cpp      # Settings: AnalyzerSettings subclass
├── XyzAnalyzerResults.h / .cpp       # Output: AnalyzerResults subclass
└── XyzSimulationDataGenerator.h / .cpp  # Testing: Simulation data generator
```

### Structure Rationale

- **One class per file pair:** Each SDK base class (Analyzer2, AnalyzerSettings, AnalyzerResults) gets its own .h/.cpp pair. This matches Saleae's own analyzers (serial-analyzer, i2c-analyzer, can-analyzer) uniformly.
- **SimulationDataGenerator separate:** The simulation generator is distinct because it is only invoked when no real capture is present. Its interface (`InitializeSimulationData`, `GenerateSimulationData`) is separate from the analysis path.
- **Settings header-only exposure:** `XyzAnalyzerSettings` is typically held by pointer in the analyzer, with other classes receiving a raw pointer for read access only. Settings owns its own lifetime.

## Architectural Patterns

### Pattern 1: SetupResults as Initialization Hook

**What:** `SetupResults()` is a virtual method on `Analyzer2` that must initialize the `mResults` object, call `SetAnalyzerResults()`, and call `mResults->AddChannelBubblesWillAppearOn()`. This was moved from `WorkerThread()` in the Analyzer2 migration.

**When to use:** Always. `SetupResults()` is called by the SDK before `WorkerThread()`. Code that was formerly at the top of `WorkerThread()` must be here.

**Why it matters:** Placing results setup in `WorkerThread()` means bubble channels are not registered before analysis begins, causing display issues.

**Example (from official serial-analyzer):**
```cpp
void SerialAnalyzer::SetupResults()
{
    mResults.reset( new SerialAnalyzerResults( this, mSettings.get() ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings->mInputChannel );
}
```

**Build order implication:** `SetupResults()` is called before `WorkerThread()`. Any member state initialized in `SetupResults()` is available to `WorkerThread()`.

### Pattern 2: Dual Frame Submission (Frame + FrameV2)

**What:** Modern analyzers submit both a legacy `Frame` (for backward compatibility and bubble display) and a `FrameV2` (for HLA interop and the data table) for every decoded result. The two are submitted together before `CommitResults()`.

**When to use:** Whenever the analyzer needs to be usable by High-Level Analyzers (HLAs) written in Python. If HLA interop is not needed, legacy `Frame` alone is sufficient.

**Why it matters:** FrameV2 is what Logic 2's data table consumes. Without it, the data table only shows basic frame info from the legacy Frame. HLAs cannot consume legacy Frame data at all.

**Example (from official serial-analyzer):**
```cpp
// Legacy Frame (required for bubble display and backward compat)
Frame frame;
frame.mStartingSampleInclusive = frame_starting_sample;
frame.mEndingSampleInclusive   = mSerial->GetSampleNumber();
frame.mData1  = data;
frame.mFlags  = parity_error ? (PARITY_ERROR_FLAG | DISPLAY_AS_ERROR_FLAG) : 0;
mResults->AddFrame( frame );

// FrameV2 (required for HLA interop and rich data table display)
FrameV2 frameV2;
frameV2.AddByteArray( "data", bytes, bytes_per_transfer );
if( parity_error )
    frameV2.AddString( "error", "parity" );
mResults->AddFrameV2( frameV2, "data", frame_starting_sample,
                      mSerial->GetSampleNumber() );

mResults->CommitResults();
ReportProgress( frame.mEndingSampleInclusive );
```

**Build order implication:** `AddFrame()` and `AddFrameV2()` must both be called before `CommitResults()`. Sample ranges must be identical between the two. Frames must be added in chronological order — never add a frame with an earlier start sample after one with a later start sample.

### Pattern 3: ClearTabularText Before AddTabularText

**What:** SDK 1.1.32+ requires `ClearTabularText()` at the start of `GenerateFrameTabularText()`. Without it, Logic 2 crashes.

**When to use:** Always, unconditionally, as the first call in `GenerateFrameTabularText()`.

**Why it matters:** This is a crash-producing omission, not just a style issue. The SDK added this requirement at 1.1.32 but the API doc has not been consistently updated.

**Example:**
```cpp
void XyzAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase display_base )
{
    ClearTabularText();  // REQUIRED to prevent crash (SDK >= 1.1.32)
    Frame frame = GetFrame( frame_index );
    // ... format and call AddTabularText()
}
```

### Pattern 4: UpdateExportProgressAndCheckForCancel in Export Loops

**What:** Long-running export loops must call `UpdateExportProgressAndCheckForCancel()` periodically to keep the UI responsive and allow the user to cancel.

**When to use:** Inside every loop that iterates over frames in `GenerateExportFile()`.

**Why it matters:** Without this call, the Logic 2 UI freezes during large exports and the cancel button has no effect.

**Example:**
```cpp
void XyzAnalyzerResults::GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id )
{
    std::ofstream out( file );
    U64 num_frames = GetNumFrames();
    for( U64 i = 0; i < num_frames; ++i )
    {
        Frame frame = GetFrame( i );
        // ... write frame data ...
        if( UpdateExportProgressAndCheckForCancel( i, num_frames ) )
            return;  // user cancelled
    }
}
```

### Pattern 5: CheckIfThreadShouldExit in WorkerThread Loop

**What:** The `WorkerThread()` infinite loop must periodically call `CheckIfThreadShouldExit()` (or the SDK's `ReportProgress()` which implicitly checks). If the thread is told to stop and doesn't check, the analyzer hangs on unload or settings change.

**When to use:** After every frame committed, or at minimum after every N frames. The official analyzers call it once per decoded frame.

**Example (from official i2c-analyzer):**
```cpp
void I2cAnalyzer::WorkerThread()
{
    mSda = GetAnalyzerChannelData( mSettings->mSdaChannel );
    mScl = GetAnalyzerChannelData( mSettings->mSclChannel );
    for( ;; )
    {
        GetByte();               // decodes one frame
        CheckIfThreadShouldExit();
    }
}
```

### Pattern 6: Settings Serialization via SimpleArchive

**What:** `LoadSettings()` and `SaveSettings()` use `SimpleArchive` to serialize/deserialize all settings fields. A version identifier string at the archive head enables format migration detection.

**When to use:** For all settings fields that must persist across Logic 2 sessions.

**Current status (important):** The Saleae SDK documentation states that "settings interfaces are serialized automatically by the Logic 2 software" and that `LoadSettings`/`SaveSettings` are "no longer used." However, this appears to apply to simple interface-backed values only. Complex analyzers that store derived state beyond interface values still use `SimpleArchive` in practice (confirmed in current TDM analyzer codebase and other third-party analyzers).

**Recommendation:** Continue using `SimpleArchive`. The auto-serialization claim in the docs is not confirmed to replace manual serialization for all cases. Add a version identifier to enable future migration.

## Data Flow

### Analysis Flow

```
Logic 2 startup
    ↓
CreateAnalyzer() → new XyzAnalyzer instance
    ↓
SetupResults()
    - new XyzAnalyzerResults
    - SetAnalyzerResults()
    - AddChannelBubblesWillAppearOn()
    ↓
WorkerThread() [runs in SDK-managed thread until killed]
    ↓
    GetSampleRate()
    GetAnalyzerChannelData() for each channel
    ↓
    [infinite loop]
    ├── Read clock/frame/data edges from AnalyzerChannelData
    ├── Accumulate bits into slot
    ├── When slot complete → AnalyzeTdmSlot()
    │     ├── Check error conditions → set frame.mFlags
    │     ├── Extract data bits (MSB/LSB order, alignment)
    │     ├── AddMarker() for visual signal markers
    │     ├── mResults->AddFrame( frame )
    │     ├── mResults->AddFrameV2( frameV2, ... )
    │     └── mResults->CommitResults()
    └── CheckIfThreadShouldExit() / ReportProgress()
```

### Export Flow

```
User clicks "Export to TXT/CSV" in Logic 2
    ↓
GenerateExportFile( file_path, display_base, export_type_user_id )
    ↓
    [NOTE: export_type_user_id is always 0 in Logic 2]
    [Custom export types declared via AddExportOption() are NOT shown in Logic 2 UI]
    ↓
    Decision on export format (via settings field, not export_type_user_id)
    ├── CSV: iterate frames → write tabular text
    └── WAV: iterate frames → PCMWaveFileHandler → binary PCM file
          └── UpdateExportProgressAndCheckForCancel() in loop
```

### Settings Flow

```
User configures analyzer in Logic 2 settings dialog
    ↓
SetSettingsFromInterfaces()
    - Read UI control values
    - Validate ranges and relationships
    - Store to member variables
    - Return false if invalid (shows error text)
    ↓
Logic 2 saves settings automatically (or via SaveSettings() / LoadSettings())
    ↓
WorkerThread() re-runs with new settings
```

## Anti-Patterns

### Anti-Pattern 1: Results Setup in WorkerThread

**What people do:** Initialize `mResults`, call `SetAnalyzerResults()`, and `AddChannelBubblesWillAppearOn()` at the top of `WorkerThread()` instead of in `SetupResults()`.

**Why it's wrong:** `SetupResults()` exists specifically to separate initialization from analysis. Logic 2 calls `SetupResults()` before `WorkerThread()`. Duplicating initialization in `WorkerThread()` means it runs redundantly on every re-run, and bubble channels may not be registered at the right time.

**Do this instead:** Move all three calls to `SetupResults()` and leave `WorkerThread()` to begin with `GetSampleRate()` and `GetAnalyzerChannelData()`.

### Anti-Pattern 2: Relying on export_type_user_id to Route Export Logic

**What people do:** Declare multiple export types with `AddExportOption()` and switch on `export_type_user_id` in `GenerateExportFile()` to produce different output formats.

**Why it's wrong:** Logic 2 does not display custom export types declared via `AddExportOption()` in its UI. The export dialog only shows "Export to TXT/CSV" and passes `export_type_user_id = 0` regardless. This feature existed in Logic 1 but is a known unimplemented feature in Logic 2 (confirmed via Saleae ideas forum and community reports as of 2025; no fix announced).

**Do this instead:** Route format selection via a settings interface field (e.g., a `AnalyzerSettingInterfaceNumberList` for CSV vs WAV). Use the `export_type_user_id` parameter value but do not depend on it being non-zero.

### Anti-Pattern 3: Missing ClearTabularText in GenerateFrameTabularText

**What people do:** Call `AddTabularText()` directly without first calling `ClearTabularText()`.

**Why it's wrong:** SDK 1.1.32+ crashes Logic 2 if `ClearTabularText()` is not called first. The old API used `ClearResultStrings()` + `AddResultString()` which is now deprecated.

**Do this instead:** Always call `ClearTabularText()` as the very first statement in `GenerateFrameTabularText()`.

### Anti-Pattern 4: Unsafe String Building with sprintf into Fixed Buffers

**What people do:** Use `sprintf`, `strcpy`, and `strcat` into fixed-size char arrays to build display strings for `AddResultString()` or bubble text.

**Why it's wrong:** If multiple error conditions are flagged simultaneously, concatenated error strings can exceed the buffer size. This is silent undefined behavior (buffer overflow). The `GenerateBubbleText` path is called per-frame at display time and is already under performance scrutiny.

**Do this instead:** Use `snprintf` with explicit size limits, or build strings with `std::string` and convert to `c_str()` for the SDK call. Alternatively, encode error state as separate FrameV2 string fields and render them in `GenerateBubbleText` by combining known-bounded substrings.

### Anti-Pattern 5: Binary Struct Serialization with Compiler-Specific Pragmas

**What people do:** Use `#pragma pack(push, 1)` and `#pragma scalar_storage_order little-endian` to write struct members directly to a binary file, relying on the compiler to produce the correct byte layout.

**Why it's wrong:** `#pragma scalar_storage_order` is a GCC extension. MSVC does not support it. Clang support is incomplete (tracked as LLVM issue #34641). The pragma creates a false sense of portability. On big-endian platforms it produces correct results; on Windows with MSVC it silently falls back to host byte order.

**Do this instead:** For binary formats like WAV, write fields explicitly using a `writeLittleEndianData(value, num_bytes)` helper that shifts and masks values into bytes. This is explicit, compiler-independent, and testable. Add `static_assert` checks for struct sizes to catch accidental layout changes at compile time.

### Anti-Pattern 6: No Progress Reporting in WorkerThread

**What people do:** Run the entire analysis loop without calling `ReportProgress()` or `CheckIfThreadShouldExit()`.

**Why it's wrong:** Logic 2 shows no progress indicator, the Cancel button has no effect, and the application appears frozen on large captures. When the user removes the analyzer, the thread does not exit cleanly.

**Do this instead:** Call `ReportProgress( frame.mEndingSampleInclusive )` after each frame. Call `CheckIfThreadShouldExit()` or rely on the SDK channel data advancing to end-of-capture to terminate the loop naturally.

## Export API: How It Should Be Used

### What Works in Logic 2

- `AddExportOption(user_id, "text")` — Registers the option internally but **does not appear in Logic 2 UI**
- `AddExportExtension(user_id, "description", "ext")` — Same: registered but not surfaced
- `GenerateExportFile(file, display_base, export_type_user_id)` — **Always called with export_type_user_id = 0** in Logic 2
- The "Export to TXT/CSV" menu item in Logic 2 triggers this with user_id = 0

### What Does Not Work in Logic 2

- Multiple named export formats (e.g., "Export to WAV", "Export to Binary") visible in the Logic 2 UI
- This is a documented missing feature, tracked on the Saleae Ideas board, with no announced fix date as of 2025

### Current Recommended Workaround

Route format selection through a settings field rather than the export type ID:

```cpp
// In XyzAnalyzerSettings constructor:
mExportTypeInterface.SetNumber( 0 );  // 0 = CSV, 1 = WAV
mExportTypeInterface.AddNumber( 0, "CSV" );
mExportTypeInterface.AddNumber( 1, "WAV" );
AddInterface( &mExportTypeInterface );
// Note: No AddExportOption() needed; Logic 2 always uses the one built-in export

// In XyzAnalyzerResults::GenerateExportFile():
if( mSettings->mExportType == 1 )
    GenerateWAV( file );
else
    GenerateCSV( file, display_base );
```

This is exactly the pattern used by the TDM analyzer already (via `mExportFileType`). The approach is sound.

## Error Handling Patterns

### SDK Error Handling Philosophy

The SDK provides no exception handling. Analyzers run in a background thread managed by Logic 2. Crashes in the analyzer thread crash Logic 2.

**Correct patterns:**
1. **Flag errors in Frame.mFlags** using the reserved `DISPLAY_AS_ERROR_FLAG` (0x80) and `DISPLAY_AS_WARNING_FLAG` (0x40) bits, plus custom flag bits for error type identification.
2. **Add visual markers** with `mResults->AddMarker(sample, MarkerType::ErrorDot, channel)` to mark the exact sample where the error occurred on the waveform.
3. **Continue analysis** after errors — do not abort the WorkerThread. The next frame sync will re-synchronize.
4. **Do not crash** — bounds-check all array accesses and buffer operations. An uncaught exception or access violation in the analyzer thread crashes Logic 2.

### Frame Flags Usage

```cpp
// Reserved by SDK (do not repurpose bits 6 and 7):
// 0x80 = DISPLAY_AS_ERROR_FLAG   (red bubble)
// 0x40 = DISPLAY_AS_WARNING_FLAG (yellow bubble)

// Custom error flags in bits 0-5 (project-defined):
#define SHORT_SLOT       (1 << 3)
#define MISSED_DATA      (1 << 2)
#define BITCLOCK_ERROR   (1 << 5)

// Combining:
frame.mFlags |= SHORT_SLOT | DISPLAY_AS_ERROR_FLAG;
```

### Settings Validation

`SetSettingsFromInterfaces()` is the correct place to validate settings. Return `false` and call `SetErrorText("message")` to surface validation errors in the settings dialog. Validate:
- Channel selection is not `UNDEFINED_CHANNEL`
- Derived values (sample rate requirements, bit period calculations) produce sensible results
- Parameter combinations that are mathematically impossible (e.g., bits per slot > slot size)

## Integration Points

### SDK Boundaries

| Boundary | Communication Pattern | Notes |
|----------|-----------------------|-------|
| Logic 2 ↔ Plugin | `extern "C"` DLL entry points | Fixed ABI; must not throw exceptions across this boundary |
| Analyzer ↔ Results | Direct pointer (mResults.get()) | AnalyzerResults is owned by Analyzer, passed by raw pointer to Settings |
| Analyzer ↔ Settings | `std::unique_ptr<Settings>` in Analyzer | Settings accessed via `mSettings.get()` in Results |
| Analyzer ↔ ChannelData | `GetAnalyzerChannelData()` returns raw pointer | Pointer valid only during WorkerThread execution |
| Results ↔ Display | SDK calls GenerateBubbleText, GenerateFrameTabularText | Called on-demand from UI thread; must be reentrant-safe |
| Results ↔ Export | SDK calls GenerateExportFile | Called from export thread; blocks until complete |

### FrameV2 and HLA Boundary

| Field Type | FrameV2 Method | HLA Access |
|------------|----------------|------------|
| Raw bytes | `AddByteArray("data", ptr, len)` | `frame.data["data"]` |
| Error label | `AddString("error", "framing")` | `frame.data["error"]` |
| Slot number | `AddInteger("slot", slot_num)` | `frame.data["slot"]` |
| Bool flag | `AddBoolean("valid", true)` | `frame.data["valid"]` |

## Build Order Implications

1. **SDK is fetched at CMake configure time** via `FetchContent`. The SDK headers must be present before the analyzer sources are compiled.
2. **`LOGIC2` preprocessor define** must be set before including any SDK header that conditionally compiles FrameV2. In CMake: `target_compile_definitions(analyzer PRIVATE LOGIC2)`. Without it, FrameV2 is unavailable.
3. **The four source file pairs are independent** — Settings does not include Analyzer, Analyzer includes Settings and Results via forward declarations where possible. Circular includes must be broken with forward declarations.
4. **`extern "C"` entry points** in `XyzAnalyzer.h` must be exported from the final shared library. CMake's `add_library(... MODULE ...)` handles this correctly; `STATIC` would not.

## Sources

- Official Saleae SampleAnalyzer: https://github.com/saleae/SampleAnalyzer
- Official serial-analyzer (reference implementation): https://github.com/saleae/serial-analyzer/blob/master/src/SerialAnalyzer.cpp
- Official i2c-analyzer (reference implementation): https://github.com/saleae/i2c-analyzer/blob/master/src/I2cAnalyzer.cpp
- AnalyzerSDK headers: https://github.com/saleae/AnalyzerSDK
- SDK migration guide: https://support.saleae.com/product/user-guide/extensions-apis-and-sdks/saleae-api-and-sdk/other-information/migrate-code-to-the-new-analyzer-sdk
- Analyzer API docs (partially outdated): https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md
- Protocol Analyzer SDK overview: https://support.saleae.com/product/user-guide/extensions-apis-and-sdks/saleae-api-and-sdk/protocol-analyzer-sdk
- Custom export types feature request (open, unresolved): https://ideas.saleae.com/b/feature-requests/custom-export-options-via-extensions/
- FrameV2 API discussion: https://discuss.saleae.com/t/framev2-api/1320
- Missing Logic 1 features in Logic 2: https://discuss.saleae.com/t/missing-v1-analyzer-sdk-plugin-features-and-ideas/684
- scalar_storage_order LLVM issue (incomplete Clang support): https://github.com/llvm/llvm-project/issues/34641

---
*Architecture research for: Saleae Logic 2 LLA plugin (TDM Analyzer audit context)*
*Researched: 2026-02-23*
