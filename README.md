# Saleae TDM Analyzer

Saleae TDM Analyzer

## Getting Started

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
mkdir build
cd build
cmake ..
cmake --build .
```

### Ubuntu 16.04

Dependencies:
- CMake 3.13+
- gcc 4.8+

Misc dependencies:

```
sudo apt-get install build-essential
```

Building the analyzer (oringinal instructions):
```
mkdir build
cd build
cmake ..
cmake --build .
# and to clean
cmake --build . --target clean
```

Building the analyzer (release):
```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release # setup for a release build
cmake --build build-release # build a release version
```

Building the analyzer (debug):
```
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug # setup for a debug build
cmake --build build-debug # build a debug version
```

Cleaning:
```
cmake --build build-debug --target clean
# -or-
cmake --build build-release --target clean
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
- Visual Studio 2015 Update 3
- CMake 3.13+

**Visual Studio 2015**

*Note - newer versions of Visual Studio should be fine.*

Setup options:
- Programming Languages > Visual C++ > select all sub-components.

Note - if CMake has any problems with the MSVC compiler, it's likely a component is missing.

**CMake**

Download and install the latest CMake release here.
https://cmake.org/download/

Building the analyzer:
```
mkdir build
cd build
cmake .. -A x64
```

Then, open the newly created solution file located here: `build\tdm_analyzer.sln`


## Output Frame Format
  
### Frame Type: `"error"`

| Property | Type | Description |
| :--- | :--- | :--- |
| `error` | str | Error details. TDM errors usually indicate the wrong number of bits inside of a frame |

TDM decode error

### Frame Type: `"data"`

| Property | Type | Description |
| :--- | :--- | :--- |
| `channel` | int | channel index. 0 or 1 |
| `data` | int | Audio value. signed or unsigned, based on TDM analyzer settings |

A single sample from a single channel

