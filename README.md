# TDM Analyzer

 TDM Analyzer for decoding TDM data.

![Example of full captured frame](pictures/full_frame.PNG)
![Example of full captured slot](pictures/valid_bits.PNG)

# Migration Guide

## v2.0.0 — FrameV2 key rename

The `"frame #"` FrameV2 field has been renamed to `"frame_number"`.
Update any HLA scripts that access this field:

```python
# Before (v1.x)
frame_number = frame.data["frame #"]

# After (v2.0+)
frame_number = frame.data["frame_number"]
```

See [CHANGELOG.md](CHANGELOG.md) for the full list of changes.

# Features

- 1 to 256 slots per frame
- Slot sizes from 2 to 64 bits
- data bits / slot from 2 to 64 bits
- Data can be exported as a `.wav` or a `.csv` file
- Frame sync can be asserted on last bit before a new frame, or on first bit of new frame
- Rising or falling frame sync sensitivity
- Rising or falling bit clock data latching
- Data can be right or left justified in the slot
- Data can be decoded most significant or least significant bit first
- Generates warnings for extra unexpected slots
- Generates warnings for truncated slots
- Searchable warnings and errors in protocol table

# Advanced Analysis features
- Checks for bitclock discrepencies and generates an error
- Identifies and marks slots with data changing that is not captured by the bitclock
- Identifies and marks missed frame sync pulses

# Settings

![Analyzer settings](pictures/analyzer_settings.PNG)

## Supported Errors and Warnings

_Note:_ Certain errors and warnings are only available with the `Advanced analysis` option enabled in the analyzer settings.  When advanced analysis is not enabled, each slot will show the sampled bits when zoomed in.  Due to the other markers placed on the serial data, these bits are not shown when advanced analysis is enabled.

![How to enable adanced analysis](pictures/advanced_analysis.PNG)

- Search for the following in the protocol table to quickly locate any errors or warnings
 - `E:` to find errors
 - `W:` to find warnings

### E: Short Slot
  - Available all the time
  - Flag `0x08`
  - The expected number of bits for the slot were not captured.  This will occur even if the missing bits are not data bits.  For example, if the expected number of bits per slot is 32, and there are 16 left justified data bits in the slot, you will receive a warning if anything less than 32 bits is counted for the slot.  This error will only ever occur on the last slot of a frame.

![Example of a short slot error](pictures/short_slot.PNG)

### E: Data Error
  - Only Available if the `Advanced analysis` option is enabled.
  - Flag `0x04`
  - If the serial data transitions twice between valid bitclock edges (meaning the data change is not detected either bitclock edge), there may be missed data.  The slot will be flagged and a marker will be placed on the suspect data.

![Example of a data error with marker](pictures/data_error.PNG)

### E: Frame Sync Missed
  - Only Available if the `Advanced analysis` option is enabled.
  - Flag `0x10`
  - The frame sync transitioned twice between valid bitclock edges and a new frame was not detected.  The slot will be flagged and a marker will be placed on the suspect frame sync.

![Example of a missed frame sync with marker](pictures/missed_framesync.PNG)

### E: Bitclock Error
  - Only Available if the `Advanced analysis` option is enabled.
  - Flag `0x20`
  - Using the sample rate, number of slots per frame, and slot size, an expected bitclock rate is calculated.  If the bitclock varies outside of this expected frequency by more than 1 Logic analyzer sample, the slot is flagged.

![Example of a bitclock error](pictures/bitclock_error.PNG)

### W: Extra Slot
  - Available all the time
  - Flag `0x02`
  - If a frame sync has not occurred and the number of slots has increased beyond the number of slots in the analyzer settings, this warning will be placed on all slots greater than the expected number of slots.

![Example of an extra slot warning](pictures/extra_slot.PNG)

_Note:_ These errors can also occur because of misconfiguration of the analyzer settings.

## Flag values

When exporting data as a CSV / TXT file, there will be a flag field.  The flags are defined as:

```c
UNEXPECTED_BITS         ( 1 << 1 ) // 0x02
MISSED_DATA             ( 1 << 2 ) // 0x04
SHORT_SLOT              ( 1 << 3 ) // 0x08
MISSED_FRAME_SYNC       ( 1 << 4 ) // 0x10
BITCLOCK_ERROR          ( 1 << 5 ) // 0x20
WARNING                 ( 1 << 6 ) // 0x40
ERROR                   ( 1 << 7 ) // 0x80
```

# Exporting data as a wave file

Logic 2 does not support custom export types for Low Level Analyzers — the only export mechanism available to analyzer plugins is the `TXT/CSV` export path. This is a confirmed Saleae design decision, not a temporary limitation. This analyzer works around this by adding an export format selector in the analyzer settings: the "Export to TXT/CSV" action produces either CSV or WAV output based on that setting. To export the captured data as a wave file, follow these steps:

1. Open the analyzer settings, click on the "Select export file type" dropdown and select "WAV" from the list. ![Analyzer setting to select the output of the TXT/CSV export option](pictures/select_export_option.PNG)
1. Save the settings
1. When analysis is complete, click on the three dots next to the analyzer and select "Export to TXT/CSV" ![exporting data](pictures/export_data.PNG)
1. Once the file is written, the contents of the file will be set based on the export file type in the analyzer settings, but the extension will always be either `.txt` or `.csv` depending on what you selected when you saved the file.  _You must change the extension yourself after the data is exported._

### Things to be aware of when exporting a wav file

- The sample rate for the exported wave file is set from the sample rate in the analyzer settings
- The number of channels is set from the number of slots in the analyzer settings.
  - If the decoded data contains more slots per frame than specified in the settings, the extra decoded slots will be ignored and not put into the wave file
  - If the decoded data contains less slots per frame than specified in the settings, the frame sync signal will be ignored and the slots will populate the wave file as if they were in order.  For example, if you specified 4 slots per frame in the settings, but the data only contains 3 slots per frame, then the wave file will be populated with 4 channels per sample consiting of:
  ```
  // F# is frame number and S# is slot #. So F3S2 is frame 3 slot 2's data
  [F1S1, F1S2, F1S3, F2S1]
  [F2S2, F2S3, F3S1, F3S2]
  [F3S3, F4S1, F4S2, F4S3] 
  ...
  ```
- Data in the wave file is stored in the following bit depth:
  ```
   2 -  8 data bits :  8 bits per channel
   9 - 16 data bits : 16 bits per channel
  17 - 32 data bits : 32 bits per channel
  33 - 40 data bits : 40 bits per channel
  41 - 48 data bits : 48 bits per channel
  49 - 64 data bits : 64 bits per channel
  ```
- Data bit settings above 32 are "supported", but the wave files generated are not likely to to open.
- For captures that fit within 4 GiB, the wave file uses a standard PCM header. For captures exceeding 4 GiB, the analyzer automatically produces an RF64 file (EBU TECH 3306) with a ds64 chunk containing the true 64-bit sizes. The threshold is computed from the capture frame count, channel count, and bit depth before export begins. RF64 files open in Audacity, FFmpeg, and other tools that support the format. Standard PCM exports have been tested to work in Audacity with channel counts from 1 to 256, and bit depths up to 32 bits. Bit depths above 32 bits do not open in Audacity with either format.
- Data bits are always scaled to ensure that the maximum values are always achievable.
  - For example, with 2 data bits, the values will map to
    ```
    0x1 -> +0.5
    0x0 ->  0
    0x3 -> -0.5
    0x2 -> -1.0
    ```
- The headers of the wave file are updated every 10 ms of audio data, so if the analyzer crashes or the export is cancelled early, the most data that will be lost is the most recently written 10 ms.

# Install instructions

[https://support.saleae.com/community/community-shared-protocols#installing-a-low-level-analyzer](https://support.saleae.com/community/community-shared-protocols#installing-a-low-level-analyzer)

# Building instructions

This project uses CMake with FetchContent to automatically download the Saleae AnalyzerSDK from GitHub during the configure step — no manual SDK installation is needed. The first `cmake` configure command handles the download (requires an internet connection on first run). The build produces a shared library (`libtdm_analyzer.so` on Linux, `libtdm_analyzer.dylib` on macOS, `tdm_analyzer.dll` on Windows) in the `Analyzers/` subdirectory of your build directory.

### MacOS

Dependencies:
- XCode with command line tools
- CMake 3.13+

Installing command line tools after XCode is installed:
```
xcode-select --install
```

Then open XCode, open Preferences from the main menu, go to locations, and select the only option under 'Command line tools'.

Installing CMake on MacOS:

1. Download the binary distribution for MacOS, `cmake-*-Darwin-x86_64.dmg`
2. Install the usual way by dragging into applications.
3. Open a terminal and run the following:
```
/Applications/CMake.app/Contents/bin/cmake-gui --install
```
*Note: Errors may occur if older versions of CMake are installed.*

Building the analyzer:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release  # Configures, downloads AnalyzerSDK via FetchContent
cmake --build build    # Compiles the analyzer shared library into build/Analyzers/
```

### Linux (Ubuntu 20.04+)

Dependencies:
- CMake 3.13+
- GCC 7+ (or Clang equivalent)

Install build dependencies:

```
sudo apt-get install build-essential cmake
```

Building the analyzer (release):
```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release  # Configures, downloads AnalyzerSDK via FetchContent
cmake --build build-release    # Compiles the analyzer shared library
```

The built library is placed in `build-release/Analyzers/libtdm_analyzer.so`.

Building the analyzer (debug):
```
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug  # Configures for debug build with debug symbols
cmake --build build-debug      # Compiles the debug analyzer shared library
```

The built library is placed in `build-debug/Analyzers/libtdm_analyzer.so`.

Cleaning:
```
cmake --build build-debug --target clean    # Removes debug build artifacts
# -or-
cmake --build build-release --target clean  # Removes release build artifacts
```

Debugging on linux with the app image:
You will need to attach gdb to a specifc running process because the *.AppImage program distributed is actually an AppImage wrapper around the Logic software. However, the launched process doesn’t load your analyzer either, it launches another instance of itself which eventually loads your analyzer.

How to identify the process you will want to debug:

1. Open the Logic 2 app and add your analyzer. (It needs to be added for the lib to be loaded). The app will load and then unload the lib once at startup to get its identification, but the shared library isn’t loaded again until you add it.
1. Run `ps ax | grep Logic`
1. There should be at least 7 matches. Several will have the path `/tmp/.mount_Logic-XXXXXX/Logic`. Of those items, look for ones that have the argument `--type=renderer`. There may be two of them. Note their process IDs.
1. To figure out which one has loaded your library, run `lsof -p <process id> | grep libtdm_analyzer.so`
1. One of the two process IDs will have a match, the other will not (see below for a command that will help with finding the PID).
1. Then, run `gdb ./libtdm_analyzer.so`. Then type `attach <process id>`.
1. `break TdmAnalyzer::WorkerThread`

_Note:_ If you run into an operation not permitted, you can run `sudo sysctl -w kernel.yama.ptrace_scope=0`

oneliner to get the proper process ID:
`ps aux | grep 'Logic' | awk '{print $2}' | xargs -I % lsof -p % | grep libtdm_analyzer.so | awk '{print $2}'`

_How it works:_ find all processes with 'Logic' in the name (`ps aux | grep 'Logic'`) and grab the second field which is the process ID (`| awk '{print $2}'`)
and pass that list to xargs which places the PID as the argument to lsof (`| xargs -I % lsof -p %`) pass this to grep to find the process
that has the libtdm_analyzer.so loaded (`| grep libtdm_analyzer.so`) then print the second field, which is the process ID (`| awk '{print $2}'`)

### How did I figure some of this stuff out?
- make a debug build with cmake : https://hsf-training.github.io/hsf-training-cmake-webpage/08-debugging/index.html
- debug with the appimage : https://discuss.saleae.com/t/failed-to-load-custom-analyzer/903/6


### Windows

Dependencies:
- Visual Studio 2019 or newer (with "Desktop development with C++" workload)
- CMake 3.13+

**Visual Studio**

Install the "Desktop development with C++" workload from the Visual Studio Installer. Older versions (2015+) also work.

**CMake**

Download and install the latest CMake release here.
https://cmake.org/download/

Building the analyzer:
```
cmake -S . -B build -A x64    # Configures for 64-bit: downloads AnalyzerSDK via FetchContent, generates a Visual Studio solution
cmake --build build --config Release    # Compiles the analyzer DLL
```

Alternatively, open the generated solution in Visual Studio:
```
build\tdm_analyzer.sln
```

The built library is placed in `build\Analyzers\tdm_analyzer.dll`.
