# Codebase Structure

**Analysis Date:** 2026-02-23

## Directory Layout

```
saleae_tdm_analyer/
├── src/                       # All source code (C++)
│   ├── TdmAnalyzer.h          # Main analyzer class definition
│   ├── TdmAnalyzer.cpp        # Main analyzer implementation (386 lines)
│   ├── TdmAnalyzerSettings.h  # Settings configuration class (82 lines)
│   ├── TdmAnalyzerSettings.cpp # Settings implementation (398 lines)
│   ├── TdmAnalyzerResults.h   # Results & export class definition (161 lines)
│   ├── TdmAnalyzerResults.cpp # Results & export implementation (529 lines)
│   ├── TdmSimulationDataGenerator.h # Test data generator class (117 lines)
│   └── TdmSimulationDataGenerator.cpp # Test data generator impl (298 lines)
├── cmake/                     # Build system scripts
│   └── ExternalAnalyzerSDK.cmake # Saleae SDK integration
├── .github/                   # GitHub workflows (CI/CD)
├── pictures/                  # Documentation screenshots
├── CMakeLists.txt             # Main build configuration
├── README.md                  # User documentation
├── azure-pipelines.yml        # Azure Pipelines CI config
└── .clang-format              # Code formatting rules
```

## Directory Purposes

**src/**
- Purpose: Complete source implementation of the TDM analyzer plugin
- Contains: Header (.h) and implementation (.cpp) files for all classes
- Key files: All 8 files - no subdirectories, flat structure
- Total: ~2,042 lines of code across 8 files

**cmake/**
- Purpose: CMake module for integrating Saleae Analyzer SDK
- Contains: ExternalAnalyzerSDK.cmake - handles SDK path detection and linking
- Generated: No
- Committed: Yes

**pictures/**
- Purpose: Screenshots for README documentation
- Contains: PNG images showing analyzer settings UI and error examples
- Generated: No (user-provided documentation images)
- Committed: Yes

**.github/**
- Purpose: GitHub-hosted CI/CD workflows
- Contains: Workflow YML files for automated builds
- Generated: No
- Committed: Yes

## Key File Locations

**Entry Points:**
- `src/TdmAnalyzer.cpp` - Contains `CreateAnalyzer()`, `DestroyAnalyzer()`, `GetAnalyzerName()` exported functions
- `src/TdmAnalyzer.h` - Plugin interface declaration with extern "C" exports (lines 67-69)

**Configuration:**
- `CMakeLists.txt` - Build configuration, sets LOGIC2 define, includes ExternalAnalyzerSDK
- `.clang-format` - Code style enforcement
- `cmake/ExternalAnalyzerSDK.cmake` - SDK integration details

**Core Logic:**
- `src/TdmAnalyzer.cpp` - Main analysis loop (`WorkerThread`, `GetTdmFrame`, `AnalyzeTdmSlot`)
- `src/TdmAnalyzer.h` - Analyzer state management (channels, current bit/frame state, bit collection vectors)
- `src/TdmAnalyzerSettings.cpp` - Settings UI interface setup (channel selection, frame/bit config, export format)
- `src/TdmSimulationDataGenerator.cpp` - Test data generation using sine/counter/static generators

**Results/Export:**
- `src/TdmAnalyzerResults.cpp` - Bubble text generation, CSV export, WAV export with PCM headers
- `src/TdmAnalyzerResults.h` - Frame flag definitions, WAV header struct definitions

**Testing:**
- `src/TdmSimulationDataGenerator.cpp/.h` - Generates synthetic TDM data for testing without hardware

## Naming Conventions

**Files:**
- Pattern: `Tdm[Purpose].cpp` and `Tdm[Purpose].h` (paired header/implementation)
- Examples: `TdmAnalyzer.h`/`TdmAnalyzer.cpp`, `TdmAnalyzerSettings.h`/`TdmAnalyzerSettings.cpp`
- Convention: PascalCase with "Tdm" prefix for consistency with Saleae SDK patterns

**Classes:**
- Pattern: `Tdm[Concept]` (e.g., TdmAnalyzer, TdmAnalyzerSettings, TdmAnalyzerResults)
- Pattern: `[Generator]` for test helpers (e.g., SineGen, CountGen, StaticGen)
- Convention: PascalCase, descriptive names

**Member Variables:**
- Pattern: `m[Name]` (prefix with 'm' for members)
- Examples: `mClockChannel`, `mDataBits`, `mCurrentFrameState`, `mSettings`
- Convention: Follows Saleae SDK style

**Functions:**
- Pattern: PascalCase with verb-noun structure
- Examples: `SetupResults()`, `WorkerThread()`, `AnalyzeTdmSlot()`, `GetNextBit()`
- Convention: Public functions descriptive, protected/private with context (Setup*, Get*, Analyze*)

**Constants/Enums:**
- Pattern: UPPERCASE_WITH_UNDERSCORES for flags/bit masks
- Examples: `SHORT_SLOT`, `MISSED_DATA`, `BITCLOCK_ERROR`
- Pattern: PascalCase for enum types
- Examples: `TdmDataAlignment`, `TdmFrameSelectInverted`, `BitGenerationState`

## Where to Add New Code

**New Feature (e.g., new error detection):**
- Primary code: `src/TdmAnalyzer.cpp` in `GetNextBit()` or `AnalyzeTdmSlot()`
- Settings: Add field to `src/TdmAnalyzerSettings.h` and UI interface to `.cpp`
- Export: Add flag definition to `src/TdmAnalyzerResults.h` and handling to `src/TdmAnalyzerResults.cpp`

**New Export Format:**
- Implementation: `src/TdmAnalyzerResults.cpp` - add new case in `GenerateExportFile()` method
- Add new enum value to `ExportFileType` in `src/TdmAnalyzerSettings.h`
- Follow pattern of `GenerateWAV()` or `GenerateCSV()` methods

**New Data Generator (for testing):**
- Class definition: `src/TdmSimulationDataGenerator.h` with GetNextValue() method
- Usage: Instantiate in `InitSineWave()` in `src/TdmSimulationDataGenerator.cpp`
- Follow pattern of `SineGen`, `CountGen`, `StaticGen` classes

**Helper/Utility Functions:**
- Location: `src/TdmAnalyzer.cpp` - protected/private methods for local analysis logic
- Location: `src/TdmSimulationDataGenerator.cpp` - private methods for test data generation
- No separate utilities file - functionality kept in relevant class implementation

## Special Directories

**build/ and build-debug/, build-release/:**
- Purpose: CMake output directories (created during build, not committed)
- Generated: Yes
- Committed: No (in .gitignore)

**.planning/codebase/:**
- Purpose: Analysis documents for GSD orchestrator
- Generated: Yes (by GSD mapper)
- Committed: Yes (documents only, no build artifacts)

**.git/, .github/**
- Purpose: Version control and CI/CD configuration
- Generated: No (.git automatic, .github files hand-written)
- Committed: Yes

**pictures/**
- Purpose: Documentation screenshots
- Generated: No
- Committed: Yes

## Architecture Enforcement

**Saleae SDK Pattern:**
- All analysis classes must inherit from Saleae `Analyzer2` or `AnalyzerResults` base classes
- Plugin interface (CreateAnalyzer, GetAnalyzerName, DestroyAnalyzer) must be exported with `extern "C"`
- All configurable parameters must use Saleae `AnalyzerSettings` interface

**Header Organization:**
- `.h` files declare interfaces, enums, structures
- `.cpp` files contain implementations
- Forward declarations used to minimize coupling (e.g., `class TdmAnalyzerSettings;` in TdmAnalyzer.h)

**Channel Data Access:**
- Channels accessed only through `AnalyzerChannelData` objects acquired in `WorkerThread()`
- No direct access to raw capture data outside WorkerThread
- Channel readers stored in member variables (`mClock`, `mFrame`, `mData`)

---

*Structure analysis: 2026-02-23*
