# Coding Conventions

**Analysis Date:** 2026-02-23

## Naming Patterns

**Files:**
- PascalCase for both header (.h) and implementation (.cpp) files
- Examples: `TdmAnalyzer.h`, `TdmAnalyzerSettings.cpp`, `TdmSimulationDataGenerator.h`
- Semantic naming directly related to the class or functionality they contain

**Functions:**
- PascalCase for all functions (both class methods and free functions)
- Examples: `WorkerThread()`, `GenerateSimulationData()`, `AnalyzeTdmSlot()`, `SetupResults()`
- Verbs preferred for action functions: `Setup*`, `Generate*`, `Get*`, `Advance*`
- No underscore prefixes for functions

**Variables:**
- camelCase for local variables and parameters
- Examples: `data_valid_sample`, `num_bits_to_process`, `starting_index`, `display_base`
- Snake_case used for local variables throughout (not camelCase)
- Member variables prefixed with 'm' (member indicator)
- Examples: `mSettings`, `mClock`, `mData`, `mResults`, `mCurrentDataState`, `mDataBits`, `mBitFlag`
- Loop counters typically use 'i' for outer loops
- Typedef'd types and structs still follow naming conventions (snake_case for members)

**Types:**
- PascalCase for classes: `TdmAnalyzer`, `TdmAnalyzerSettings`, `TdmAnalyzerResults`
- ALL_CAPS for #define constants and bit flags: `UNEXPECTED_BITS`, `SHORT_SLOT`, `MISSED_DATA`, `BITCLOCK_ERROR`
- enum types in PascalCase: `TdmDataAlignment`, `TdmBitAlignment`, `ExportFileType`, `BitGenerationState`
- enum values in ALL_CAPS or PascalCase depending on convention: `BITS_SHIFTED_RIGHT_1`, `NO_SHIFT`, `LEFT_ALIGNED`, `RIGHT_ALIGNED`

## Code Style

**Formatting:**
- Formatter: clang-format (configuration in `.clang-format`)
- Indentation: 4 spaces (no tabs)
- Column limit: 140 characters
- Braces: Allman style (opening brace on new line)
- No blocks on single line (AllowShortBlocksOnASingleLine: false)
- No single-line if statements (AllowShortIfStatementsOnASingleLine: false)
- Pointer alignment: left (spaces before pointer: `*data` not `* data`)
- Spaces in parentheses: yes (`( value )` not `(value)`)
- No spaces in empty parentheses: `()` not `( )`

**Linting:**
- No dedicated linter configuration found; rely on clang-format for style enforcement
- Compiler warnings disabled selectively with `#pragma warning( disable : XXXX )` where needed
  - Example: `#pragma warning( disable : 4251 )` for DLL interface warnings
  - Example: `#pragma warning( disable : 4996 )` for unsafe function warnings (sprintf)

**Code example - typical formatting:**
```cpp
void TdmAnalyzer::AnalyzeTdmSlot()
{
    U64 result = 0;
    U32 starting_index = 0;
    size_t num_bits_to_process = mDataBits.size();

    // begin a new frame
    mResultsFrame.mFlags = 0;
    mResultsFrame.mStartingSampleInclusive = 0;
    mResultsFrame.mEndingSampleInclusive = 0;

    if( mSlotNum >= mSettings->mSlotsPerFrame )
    {
        mResultsFrame.mFlags |= UNEXPECTED_BITS | DISPLAY_AS_WARNING_FLAG;
    }
}
```

## Import Organization

**Order:**
1. Standard C/C++ headers (e.g., `<iostream>`, `<fstream>`, `<cstring>`)
2. Math/utility headers (e.g., `<math.h>`)
3. Algorithm/container headers (e.g., `<algorithm>`, `<vector>`)
4. Saleae SDK headers (e.g., `<Analyzer.h>`, `<AnalyzerHelpers.h>`)
5. Project-local headers in quotes (e.g., `"TdmAnalyzer.h"`)

**Examples from codebase:**
```cpp
// From TdmAnalyzer.cpp
#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include <AnalyzerChannelData.h>

// From TdmAnalyzerResults.cpp
#include "TdmAnalyzerResults.h"
#include <AnalyzerHelpers.h>
#include "TdmAnalyzer.h"
#include "TdmAnalyzerSettings.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <cstring>

// From TdmSimulationDataGenerator.cpp
#include "TdmSimulationDataGenerator.h"
#include <math.h>
#include <algorithm>
#include <fstream>
#include <iostream>
```

**Path Aliases:**
- No path aliases detected; all includes use angle brackets for SDK headers or quotes for local headers

## Error Handling

**Patterns:**
- No exceptions used; relies on return codes and conditional logic
- Validation typically done by checking preconditions before processing
- Warnings and errors flagged using bit flags on frame structures
  - Example: `mResultsFrame.mFlags |= SHORT_SLOT | DISPLAY_AS_ERROR_FLAG;`
- Error/warning display determined by flag combinations
  - `DISPLAY_AS_ERROR_FLAG` (0x80) and `DISPLAY_AS_WARNING_FLAG` (0x40) used to mark severity
- Channel data bounds checked via methods like `WouldAdvancingToAbsPositionCauseTransition()`
- No try-catch blocks; analysis continues with error flags set

**Example from `TdmAnalyzer.cpp`:**
```cpp
if( num_bits_to_process < mSettings->mBitsPerSlot )
{
    mResultsFrame.mFlags |= SHORT_SLOT | DISPLAY_AS_ERROR_FLAG;
}
else
{
    // process normally
}
```

## Logging

**Framework:** Not used in production code
- Some debug/diagnostic use of `<iostream>` in `TdmSimulationDataGenerator.cpp` (not active in release)
- No logging framework; output goes directly to stdout or file I/O where needed

## Comments

**When to Comment:**
- Block comments describe the purpose and state at entry/exit of functions
- Inline comments explain non-obvious logic or state transitions
- Comments describe the algorithm's current position and expectations
- Examples from `TdmAnalyzer.cpp`:
  ```cpp
  // we enter the function with the clock state such that on the next edge is where the data is valid.
  mClock->AdvanceToNextEdge(); // R: low -> high / F: high -> low
  U64 data_valid_sample = mClock->GetSampleNumber(); // R: high / F: low

  // on entering this function:
  // we are at the beginning of a new TDM frame
  // mCurrentFrameState and State are the values of the first bit -- that belongs to us -- in the TDM frame.
  // mLastFrameState and mLastDataState are the values from the bit just before.
  ```

**JSDoc/TSDoc:**
- Not used; this is a C++ analyzer plugin, not a TypeScript/JavaScript project
- Function declarations in headers are self-documenting via naming and parameter types

## Function Design

**Size:**
- Functions range from 10-50 lines typically
- Worker thread loop (`WorkerThread()`) is naturally unbounded (infinite loop for analyzer)
- Helper functions like `GetNextBit()` are 50+ lines when including detailed state management

**Parameters:**
- Use references for output parameters (e.g., `BitState& data, BitState& frame, U64& sample_number`)
- Const references for read-only complex types
- Pointer-based ownership for dynamically allocated resources (wrapped in `std::unique_ptr`)
- Example from `TdmAnalyzer.h`:
  ```cpp
  void GetNextBit( BitState& data, BitState& frame, U64& sample_number );
  ```

**Return Values:**
- U32 return codes for counts and sizes
- void for side-effect operations
- Frame/data structures returned by value from getter methods
- Unsigned integer types used throughout (U32, U64, U8, U16) from Saleae SDK

## Module Design

**Exports:**
- C++ classes exported via public inheritance from Saleae base classes
  - `TdmAnalyzer` extends `Analyzer2`
  - `TdmAnalyzerResults` extends `AnalyzerResults`
  - `TdmAnalyzerSettings` extends `AnalyzerSettings`
- C exports for DLL interface (extern "C" in `TdmAnalyzer.h`):
  ```cpp
  extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
  extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
  extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );
  ```

**Barrel Files:**
- Not applicable; this is a compiled C++ plugin, not a modular JavaScript/TypeScript project
- Headers serve as the public interface for inter-module communication

## Include Guards

**Pattern:**
- Standard `#ifndef` style include guards
- Format: `#ifndef [FILENAME_UPPERCASE]` and `#define [FILENAME_UPPERCASE]`
- Examples:
  ```cpp
  #ifndef TDM_ANALYZER_H
  #define TDM_ANALYZER_H
  // ...
  #endif // TDM_ANALYZER_H
  ```

---

*Convention analysis: 2026-02-23*
