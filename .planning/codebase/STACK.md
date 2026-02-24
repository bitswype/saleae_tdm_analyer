# Technology Stack

**Analysis Date:** 2026-02-23

## Languages

**Primary:**
- C++ (C++11 standard) - Entire analyzer implementation (`src/TdmAnalyzer.cpp`, `src/TdmAnalyzer.h`, `src/TdmAnalyzerResults.cpp`, `src/TdmAnalyzerSettings.cpp`, `src/TdmSimulationDataGenerator.cpp`)

**Secondary:**
- CMake (3.11+) - Build configuration (`CMakeLists.txt`, `cmake/ExternalAnalyzerSDK.cmake`)
- YAML - CI/CD configuration (`azure-pipelines.yml`, `.github/workflows/build.yml`)

## Runtime

**Environment:**
- Platform-specific native binaries:
  - Windows: DLL (`.dll`) - built with Visual Studio 2015+
  - macOS: Shared object (`.so`) - supports both x86_64 and ARM64 architectures
  - Linux: Shared object (`.so`) - compiled with GCC 4.8+

**Plugin Architecture:**
- Saleae Logic Analyzer 2 application
- Loads as a low-level protocol analyzer plugin via Saleae AnalyzerSDK

## Frameworks

**Core:**
- Saleae AnalyzerSDK (fetched from https://github.com/saleae/AnalyzerSDK.git, master branch) - Protocol analyzer framework providing base classes (`Analyzer2`, `AnalyzerResults`) and channel data interfaces

**Build/Dev:**
- CMake 3.11+ - Build system with FetchContent for dependency management
- Clang Format - Code formatting configuration (`.clang-format`) for Logic style enforcement

## Key Dependencies

**Critical:**
- Saleae AnalyzerSDK (dynamic) - Provides core analyzer infrastructure, symbol exports, and channel data interfaces
  - Linked via CMake's `FetchContent` from GitHub
  - Custom CMake module: `cmake/ExternalAnalyzerSDK.cmake`
  - Defines: `Analyzer2`, `AnalyzerResults`, `AnalyzerChannelData`, `AnalyzerSettings`, `AnalyzerTypes`, `AnalyzerHelpers`
  - On Windows/macOS: SDK library is copied to output directory during build

**Standard Library:**
- `<iostream>` - Console output (TdmAnalyzerResults.cpp, TdmSimulationDataGenerator.cpp)
- `<fstream>` - File I/O for CSV/WAV export (TdmAnalyzerResults.cpp, TdmSimulationDataGenerator.cpp)
- `<sstream>` - String stream for serialization (TdmAnalyzerSettings.cpp, TdmAnalyzerResults.cpp)
- `<vector>` - Dynamic arrays for bit/flag storage (TdmAnalyzer.h)
- `<algorithm>` - Standard algorithms (TdmSimulationDataGenerator.cpp)
- `<memory>` - `std::unique_ptr` for resource management (TdmAnalyzer.h, TdmAnalyzerSettings.h)
- `<cstring>` - String operations (TdmAnalyzerSettings.cpp, TdmAnalyzerResults.cpp)
- `<math.h>` - Math functions for signal simulation (TdmSimulationDataGenerator.cpp, TdmSimulationDataGenerator.h)

## Configuration

**Environment:**
- No runtime environment variables required
- Build-time configuration via CMake variables:
  - `CMAKE_BUILD_TYPE`: Release, Debug, or RelWithDebInfo
  - `CMAKE_OSX_ARCHITECTURES`: macOS architecture selection (x86_64, arm64)
  - `CMAKE_CXX_STANDARD`: Fixed to C++11
  - `DCMAKE_EXPORT_COMPILE_COMMANDS`: Enabled for IDE support

**Code Style:**
- Clang Format configuration in `.clang-format`:
  - Language: C++
  - IndentWidth: 4 spaces
  - ColumnLimit: 140 characters
  - BreakBeforeBraces: Allman style
  - TabWidth: 4 (but uses spaces, not tabs)
  - PointerAlignment: Left

## Platform Requirements

**Development:**

**macOS:**
- XCode with command line tools
- CMake 3.13+

**Windows:**
- Visual Studio 2015 Update 3+ (or newer)
- CMake 3.13+

**Linux:**
- CMake 3.13+
- GCC 4.8+ (tested with gcc-14 in CI)
- build-essential package

**Production (Deployment):**
- Saleae Logic Analyzer 2 application (any supported version)
- Plugin installed to Logic analyzer plugin directory
- Installation instructions: https://support.saleae.com/community/community-shared-protocols#installing-a-low-level-analyzer

## Build Output

**Artifacts:**
- Windows: `build/Analyzers/Release/*.dll` or `build/Analyzers/RelWithDebInfo/*.dll`
- macOS: `build/[x86_64|arm64]/Analyzers/*.so`
- Linux: `build/Analyzers/*.so`

**Output Structure:**
- Binaries placed in `Analyzers/` subdirectory per platform
- macOS also generates debug symbols alongside `.so` files

## Compiler Flags

**Preprocessor:**
- `-DLOGIC2` - Defines Logic 2 as the target environment (CMakeLists.txt)
- `-D_USE_MATH_DEFINES` - Enables math constants like M_PI in TdmSimulationDataGenerator.h

**Warnings:**
- Windows-specific: `#pragma warning(disable: 4251)` to suppress DLL interface warnings on exported classes (TdmAnalyzer.h, TdmAnalyzerResults.h)

## Build System Details

**CMake Features Used:**
- `FetchContent` module for automatic AnalyzerSDK download
- Custom CMake function `add_analyzer_plugin()` defined in `cmake/ExternalAnalyzerSDK.cmake`
- Install targets to `Analyzers/` directory
- Module-based library construction (add_library with MODULE keyword)

**Multi-Platform Builds:**
- Windows: Visual Studio generator with x64 platform
- macOS: Separate builds for x86_64 and arm64 architectures
- Linux: Unix Makefiles generator with Release build type

---

*Stack analysis: 2026-02-23*
