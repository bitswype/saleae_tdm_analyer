# External Integrations

**Analysis Date:** 2026-02-23

## APIs & External Services

**Saleae Logic Analyzer 2:**
- Integration method: Native plugin architecture via AnalyzerSDK
  - Analyzer inherits from `Analyzer2` class (TdmAnalyzer.h)
  - Loads as `.dll` (Windows), `.so` (macOS/Linux) into Logic 2 application
  - No network APIs; purely local plugin integration
  - Symbol exports: `GetAnalyzerName()`, `CreateAnalyzer()`, `DestroyAnalyzer()` (TdmAnalyzer.h, lines 67-69)

## Data Storage

**Input Data:**
- Logic Analyzer capture data via AnalyzerChannelData interface
  - Three channels: Clock (mClock), Frame Sync (mFrame), Data (mData)
  - Sourced from logic analyzer hardware capture, not file-based
  - Accessed through Saleae AnalyzerSDK: `GetAnalyzerChannelData(channel)` (TdmAnalyzer.cpp, lines 34-36)

**Output Data:**
- File export to local filesystem:
  - CSV/TXT format: Tabular data with frame info, slot data, and error flags (TdmAnalyzerResults.cpp - GenerateCSV method)
  - WAV format: PCM audio file for multi-channel audio export (TdmAnalyzerResults.cpp - GenerateWAV method, TdmAnalyzerResults.h)
  - User selects export type via analyzer settings UI (ExportFileType enum in TdmAnalyzerSettings.h, lines 23-27)

**Databases:**
- None. This is a stateless protocol analyzer without persistent data storage.

**File Storage:**
- Local filesystem only
- User configures output location via Logic 2 export dialog
- WAV and CSV handlers in TdmAnalyzerResults.cpp manage file I/O via `std::ofstream`

**Caching:**
- None. All analysis is performed on-demand during capture playback.

## Authentication & Identity

**Auth Provider:**
- None. Plugin runs within Logic 2 application context with no external authentication required.

## Monitoring & Observability

**Error Tracking:**
- No external error reporting
- Protocol analysis errors/warnings stored in frame results and exported via CSV/WAV
- Error flags defined in TdmAnalyzerResults.h (lines 9-14):
  - `UNEXPECTED_BITS` (0x02)
  - `MISSED_DATA` (0x04)
  - `SHORT_SLOT` (0x08)
  - `MISSED_FRAME_SYNC` (0x10)
  - `BITCLOCK_ERROR` (0x20)

**Logs:**
- Console output via `std::cout` and `std::cerr` in simulation data generator (TdmSimulationDataGenerator.cpp)
- Debug logging available via gdb when attached to Logic 2 renderer process (see README.md debugging instructions)

## CI/CD & Deployment

**Hosting:**
- GitHub releases via GitHub Actions (`.github/workflows/build.yml`)
- Alternative CI: Azure Pipelines (`azure-pipelines.yml`)

**CI Pipeline:**

**GitHub Actions (.github/workflows/build.yml):**
- Triggers: Push to main, all tags, PRs, manual workflow dispatch
- Builds: Windows (latest), macOS (latest), Linux (latest)
- Test environments:
  - Windows: `windows-latest` runner
  - macOS: `macos-latest` runner (builds for both x86_64 and arm64)
  - Linux: `ubuntu-latest` runner with GCC 14
- Artifacts uploaded to GitHub Actions artifact storage
- Release creation: Automatic GitHub release on tag push via `softprops/action-gh-release@v1`
- Release artifact: `analyzer.zip` containing platform-specific binaries

**Azure Pipelines (azure-pipelines.yml):**
- Triggers: Commits to master, all PRs, all tags
- Builds: Windows (VS 2017), macOS (10.15), Linux (Ubuntu 18.04)
- Service connection: GitHub integration for release publication (`github.com_Marcus10110`)
- Artifacts: Platform-specific binaries organized into win/osx/linux subdirectories
- GitHub Release task: Publishes tagged builds to GitHub releases

**Build Artifacts:**
- Windows: `*.dll` (Release configuration)
- macOS: `*.so` (both x86_64 and arm64)
- Linux: `*.so`
- Packaged as: `analyzer.zip` for distribution

**Installation:**
- Users download from GitHub releases
- Follow Saleae documentation: https://support.saleae.com/community/community-shared-protocols#installing-a-low-level-analyzer

## Environment Configuration

**Required env vars:**
- None at runtime for the analyzer plugin itself

**Build-time env vars (GitHub Actions only):**
- `CC` set to `gcc-14` (Linux builds)
- `CXX` set to `g++-14` (Linux builds)

**Secrets location:**
- GitHub Actions: Service connection `github.com_Marcus10110` for Azure release publication
- No environment variables stored; connection managed via GitHub Actions service connection configuration

## Webhooks & Callbacks

**Incoming:**
- None. Plugin receives control flow from Logic 2 application:
  - `WorkerThread()` - Called by analyzer framework to process captured data (TdmAnalyzer.cpp)
  - `SetupResults()` - Called to initialize results interface (TdmAnalyzer.cpp)
  - `GenerateSimulationData()` - Called to generate test data (TdmAnalyzer.h)
  - `NeedsRerun()` - Called to determine if re-analysis required (TdmAnalyzer.h)

**Outgoing:**
- None. Plugin reports results back to Logic 2 via AnalyzerResults interface
- Results bubbles, export data, and tabular displays populated via:
  - `GenerateBubbleText()` (TdmAnalyzerResults.cpp)
  - `GenerateExportFile()` (TdmAnalyzerResults.cpp)
  - `GenerateFrameTabularText()` (TdmAnalyzerResults.cpp)

## Interface Contracts with Saleae AnalyzerSDK

**Class Inheritance:**
- `TdmAnalyzer` extends `Analyzer2` (TdmAnalyzer.h, line 9)
- `TdmAnalyzerResults` extends `AnalyzerResults` (TdmAnalyzerResults.h, line 24)
- `TdmAnalyzerSettings` extends `AnalyzerSettings` (TdmAnalyzerSettings.h, line 29)

**Required Virtual Method Overrides:**
- `SetupResults()` - Initialize result handlers (TdmAnalyzer.cpp)
- `WorkerThread()` - Perform protocol analysis (TdmAnalyzer.cpp)
- `GenerateSimulationData()` - Create test capture data (TdmAnalyzer.cpp)
- `GetMinimumSampleRateHz()` - Report minimum sample rate requirement (TdmAnalyzer.h)
- `GetAnalyzerName()` - Return display name (TdmAnalyzer.cpp)
- `NeedsRerun()` - Determine if reanalysis needed (TdmAnalyzer.h)
- `SetSettingsFromInterfaces()` - Validate and load UI settings (TdmAnalyzerSettings.cpp)
- `LoadSettings()` - Deserialize persisted settings (TdmAnalyzerSettings.cpp)
- `SaveSettings()` - Serialize current settings (TdmAnalyzerSettings.cpp)
- `GenerateBubbleText()` - Generate tooltip text (TdmAnalyzerResults.cpp)
- `GenerateExportFile()` - Export captured data (TdmAnalyzerResults.cpp)
- `GenerateFrameTabularText()` - Generate table rows (TdmAnalyzerResults.cpp)

---

*Integration audit: 2026-02-23*
