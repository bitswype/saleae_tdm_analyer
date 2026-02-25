# Stack Research

**Domain:** Saleae Logic 2 Low-Level Protocol Analyzer Plugin (C++ SDK)
**Researched:** 2026-02-24 (updated for v1.4 milestone — SDK modernization, FrameV2, RF64, sample rate)
**Confidence:** HIGH (SDK headers fetched from GitHub; RF64 confirmed from libsndfile/FFmpeg; FrameV2 confirmed in codebase)

---

## v1.4 Milestone: What Changes and What Does Not

This section answers the four specific research questions for the current milestone. The sections below it preserve the full v1.0 stack baseline.

### Q1: SDK Update — Latest Commit / What Changed Since July 2023

**Finding: No update available.**

```
Current pin:  114a3b8306e6a5008453546eda003db15b002027  (2023-07-07)
Remote HEAD:  114a3b8306e6a5008453546eda003db15b002027  (2023-07-07)
```

Verified via `git ls-remote https://github.com/saleae/AnalyzerSDK.git HEAD master`.

The "SDK audit and update" task resolves to: **already at latest, no update needed.** Document this and close the task. The CMake pin in `ExternalAnalyzerSDK.cmake` is correct and already points to HEAD.

**Confidence:** HIGH — direct remote query.

---

### Q2: FrameV2 API — Already Fully Implemented

**Finding: FrameV2 is complete in the codebase. No new implementation needed.**

The existing code in `src/TdmAnalyzer.cpp` already does:

```cpp
// In TdmAnalyzer constructor:
UseFrameV2();  // non-virtual public method on Analyzer base class

// In AnalyzeTdmSlot():
FrameV2 frame_v2;
frame_v2.AddInteger( "channel", mResultsFrame.mType );   // S64 → slot number
frame_v2.AddInteger( "data",    adjusted_value );        // S64 → decoded PCM value
frame_v2.AddString(  "errors",  error_str );             // null-terminated C string
frame_v2.AddString(  "warnings", warning_str );          // null-terminated C string
frame_v2.AddInteger( "frame #", mFrameNum );             // S64 → TDM frame counter
mResults->AddFrameV2( frame_v2, "slot",
    mResultsFrame.mStartingSampleInclusive,
    mResultsFrame.mEndingSampleInclusive );
```

**Complete FrameV2 API from SDK header (`include/AnalyzerResults.h`, compiled under `#ifdef LOGIC2`):**

```cpp
class LOGICAPI FrameV2
{
  public:
    FrameV2();
    ~FrameV2();

    void AddString    ( const char* key, const char* value );
    void AddDouble    ( const char* key, double value );
    void AddInteger   ( const char* key, S64 value );
    void AddBoolean   ( const char* key, bool value );
    void AddByte      ( const char* key, U8 value );
    void AddByteArray ( const char* key, const U8* data, U64 length );

    FrameV2Data* mInternals;
};

// In AnalyzerResults class:
void AddFrameV2( const FrameV2& frame, const char* type,
                 U64 starting_sample, U64 ending_sample );
```

**One cleanup item:** `TdmAnalyzer.h` line 40 declares `FrameV2 mResultsFrameV2` as a member variable. This is unused dead code — `AnalyzeTdmSlot()` uses a local stack `FrameV2` instead. Remove the dead member to eliminate confusion.

**Confidence:** HIGH — header fetched from GitHub; code confirmed in local source.

---

### Q3: RF64 Format Structure — New Handler Needed

**Finding: RF64 requires a new `RF64WaveFileHandler` class. The existing `PCMWaveFileHandler` uses 32-bit RIFF and cannot be extended; a parallel RF64 class is needed.**

#### RF64 File Layout (EBU Tech 3306, confirmed by libsndfile and FFmpeg)

For PCM format (`mFormatTag = 0x0001`, `fmt ck size = 16`):

```
OFFSET  SIZE  FIELD            VALUE / NOTES
------  ----  -----            -----
0       4     RF64 ID          "RF64"
4       4     RIFF ck size     0xFFFFFFFF  (sentinel; actual size in ds64.riffSize)
8       4     WAVE ID          "WAVE"

--- ds64 chunk: 8-byte header + 28-byte payload = 36 bytes total ---
12      4     ds64 ck ID       "ds64"
16      4     ds64 ck size     28  (little-endian U32, fixed)
20      8     riffSize         file_size - 8       (U64 LE, updated on close)
28      8     dataSize         audio_data_bytes    (U64 LE, updated on close)
36      8     sampleCount      total_audio_frames  (U64 LE, updated on close)
44      4     tableLength      0   (no extended table entries)

--- fmt chunk: standard PCM ---
48      4     fmt  ck ID       "fmt "
52      4     fmt  ck size     16  (little-endian U32)
56      2     mFormatTag       0x0001 (PCM)
58      2     mNumChannels     N
60      4     mSamplesPerSec   sample_rate
64      4     mBytesPerSec     sample_rate * frame_size
68      2     mBlockSizeBytes  bytes_per_channel * num_channels
70      2     mBitsPerSample   8/16/24/32/...

--- data chunk ---
72      4     data ck ID       "data"
76      4     data ck size     0xFFFFFFFF  (sentinel; actual size in ds64.dataSize)
80      ...   audio data       streaming PCM samples
```

**Total header size before audio:** 80 bytes.

**seekp() update positions (write U64 little-endian at close):**

```cpp
constexpr U64 RF64_DS64_RIFFSIZE_POS  = 20;  // 8-byte riffSize in ds64 payload
constexpr U64 RF64_DS64_DATASIZE_POS  = 28;  // 8-byte dataSize in ds64 payload
constexpr U64 RF64_DS64_SAMPLECNT_POS = 36;  // 8-byte sampleCount in ds64 payload
// data ck size at offset 76 is written once (0xFFFFFFFF) at construction; not updated.
```

**Update values on close:**
```
riffSize    = total_file_size - 8
dataSize    = total_frames * frame_size_bytes   (+ 1 if odd, for padding byte)
sampleCount = total_frames
```

#### struct definition pattern (C++11, #pragma pack(1)):

```cpp
#pragma scalar_storage_order little-endian
#pragma pack(push, 1)
typedef struct
{
    char   mRF64CkId[4]    = {'R','F','6','4'};  // @ 0
    U32    mRiffCkSize      = 0xFFFFFFFF;         // @ 4  sentinel
    char   mWaveId[4]       = {'W','A','V','E'};  // @ 8
    char   mDs64CkId[4]     = {'d','s','6','4'};  // @ 12
    U32    mDs64CkSize      = 28;                 // @ 16
    U64    mDs64RiffSize    = 0;                  // @ 20  updated on close
    U64    mDs64DataSize    = 0;                  // @ 28  updated on close
    U64    mDs64SampleCount = 0;                  // @ 36  updated on close
    U32    mDs64TableLength = 0;                  // @ 44
    char   mFmtCkId[4]      = {'f','m','t',' '}; // @ 48
    U32    mFmtCkSize       = 16;                 // @ 52
    U16    mFormatTag       = 0x0001;             // @ 56
    U16    mNumChannels     = 1;                  // @ 58
    U32    mSamplesPerSec   = 48000;              // @ 60
    U32    mBytesPerSec     = 96000;              // @ 64
    U16    mBlockSizeBytes  = 2;                  // @ 68
    U16    mBitsPerSample   = 16;                 // @ 70
    char   mDataCkId[4]     = {'d','a','t','a'};  // @ 72
    U32    mDataCkSize      = 0xFFFFFFFF;         // @ 76  sentinel
    /* audio data starts @ 80 */
} WaveRF64PCMHeader;
#pragma pack(pop)
#pragma scalar_storage_order default

static_assert(sizeof(WaveRF64PCMHeader) == 80,
    "WaveRF64PCMHeader must be 80 bytes. Check pack(1) and field types.");
```

#### Write strategy: always-RF64, not upgrade-on-close

Write "RF64" and "ds64" from file open. Do not start as RIFF and convert on close. Reasons:
1. Simpler code — no conditional format switching
2. All modern audio software (Audacity, REAPER, Python scipy, ffmpeg) reads RF64
3. Consistent with the existing handler pattern (write header at construction, update size fields periodically and on close)
4. The existing 4 GiB guard can be removed entirely

**Confidence:** HIGH — RF64 layout confirmed against libsndfile `src/rf64.c` and FFmpeg `libavformat/wavenc.c` (both authoritative implementations of EBU Tech 3306).

---

### Q4: Sample Rate Sanity Check — Already Implemented via GetMinimumSampleRateHz()

**Finding: The mechanism is already implemented. Verification and documentation are the action items, not new code.**

#### GetSampleRate() API

```cpp
// Non-virtual public method on Analyzer base class (from include/Analyzer.h):
U32 GetSampleRate();            // returns current capture sample rate in Hz
U32 GetSimulationSampleRate();  // returns simulation sample rate in Hz
```

Already called at `WorkerThread()` line 40 and stored in `mSampleRate` (U64 member). Also called in `GenerateCSV()` via `mAnalyzer->GetSampleRate()`.

**Important:** `GetSampleRate()` is NOT available in `AnalyzerSettings::SetSettingsFromInterfaces()` — that method runs before the analyzer has a capture context. Sample rate is only available from inside the `Analyzer` subclass.

#### GetMinimumSampleRateHz() — the designed warning mechanism

```cpp
// Already implemented in TdmAnalyzer.cpp:
U32 TdmAnalyzer::GetMinimumSampleRateHz()
{
    return mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate * 4;
}
```

Logic 2 calls this method and displays a warning in the settings panel if the capture sample rate is below this value. This is the designed SDK mechanism for sample rate warnings.

The formula `slots * bits * frame_rate * 4` gives a 4x oversampling requirement relative to the bit clock frequency, which is the correct Nyquist-times-4 heuristic.

**Action items:**
1. Verify Logic 2 visibly surfaces the warning (manual test)
2. Confirm the formula is correct for all TDM configurations
3. Consider whether a warning bubble frame inside `WorkerThread()` would add value when `GetSampleRate() < GetMinimumSampleRateHz()` — this is extra, not required

**Confidence:** HIGH — `GetSampleRate()` confirmed in Analyzer.h; `GetMinimumSampleRateHz()` confirmed in SampleAnalyzer API docs and existing codebase.

---

## Full Stack Baseline (from v1.0 research, still current)

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| Saleae AnalyzerSDK | master @ `114a3b8` (Jul 7, 2023) | Plugin base classes, channel data interfaces | Only SDK supported by Logic 2; no alternative |
| C++ | C++11 | Plugin implementation language | SDK mandates C++11; going newer risks compatibility with the precompiled SDK library |
| CMake | 3.13+ | Build system | Required by SDK's FetchContent setup; 3.13 adds `FetchContent_MakeAvailable` convenience |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| FrameV2 API (built into SDK) | Available since Logic 2.3.43 | Structured per-frame data visible in Logic 2 data table | Already in use — no additional setup needed |
| Standard C++ library only | C++11 stdlib | File I/O, string ops, math | No third-party libraries are needed or appropriate |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| GCC 14 (Linux), MSVC 2017+ (Windows), Xcode (macOS) | Compilation | CI uses gcc-14; SDK requires 64-bit targets only |
| GitHub Actions v4 | CI/CD | `actions/checkout@v4`, `upload-artifact@v4` — current in TDM analyzer |
| Clang Format | Code style enforcement | Allman braces, 4-space indent, 140-char line limit |

---

## SDK State: What Has and Has Not Changed

### SDK Repository (github.com/saleae/AnalyzerSDK)

**Last commit:** July 7, 2023 (`114a3b8`) — added macOS ARM64 support.

**Tags:** Only one tag: `alpha-1`. No stable release tags. Convention is `master`.

**Conclusion:** SDK has been stable (no API changes) since July 2023. No breaking changes pending.

### Logic 2 Application

**Current version:** 2.4.40 (December 17, 2025). No SDK-level changes in recent 2.4.x releases.

---

## Critical: Custom Export Types Still Broken in Logic 2

**Status as of Logic 2.4.40:** CONFIRMED NOT FIXED.

`AddExportOption()` and `AddExportExtension()` exist in the SDK but are not honored by Logic 2. The WAV export workaround (hijacking the TXT/CSV export callback) is still necessary and correct.

---

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| External WAV libraries (libsndfile, dr_wav) | No external dependencies in SDK plugin pattern; adds CMake complexity | Native struct + handler (established pattern) |
| Upgrade RIFF to RF64 on close | Asymmetric — file reads as "RIFF" during write; more complex logic | Write "RF64" from construction always |
| `SetErrorText()` for sample rate warning | Returns false from `SetSettingsFromInterfaces()` → blocks the analyzer entirely | `GetMinimumSampleRateHz()` which Logic 2 handles non-blockingly |
| `std::auto_ptr` | Deprecated in C++11, removed in C++17 | `std::unique_ptr` (already used throughout) |
| `GIT_TAG master` in production | Non-reproducible; any Saleae push could break builds | Pin to specific commit hash (already done) |

---

## Version Compatibility

| Component | Compatible With | Notes |
|-----------|-----------------|-------|
| AnalyzerSDK `114a3b8` | Logic 2.3.43+ | FrameV2 API requires 2.3.43+; already in use |
| C++11 | MSVC 2015+, GCC 4.8+, Clang 3.3+ | SDK CMake fatally rejects 32-bit targets |
| RF64 PCM struct | C++11 | `static_assert`, `constexpr`, `#pragma pack(1)` — all C++11 |
| `std::ofstream::seekp(U64)` | C++11 | Used in existing handlers; seekp to update ds64 fields |

---

## Sources

- `git ls-remote https://github.com/saleae/AnalyzerSDK.git` — SDK HEAD confirmed 114a3b8 still latest (HIGH confidence)
- `https://api.github.com/repos/saleae/AnalyzerSDK/commits` — commit history, last commit 2023-07-07 (HIGH confidence)
- `https://raw.githubusercontent.com/saleae/AnalyzerSDK/master/include/Analyzer.h` — `UseFrameV2()`, `GetSampleRate()`, `GetMinimumSampleRateHz()` signatures (HIGH confidence)
- `https://raw.githubusercontent.com/saleae/AnalyzerSDK/master/include/AnalyzerResults.h` — `FrameV2` class, `AddFrameV2()` signature (HIGH confidence)
- `https://github.com/saleae/can-analyzer/blob/master/src/CanAnalyzer.cpp` — FrameV2 usage pattern (HIGH confidence — official Saleae example)
- `https://github.com/saleae/SampleAnalyzer/blob/master/docs/Analyzer_API.md` — `SetErrorText`, `GetMinimumSampleRateHz` patterns (HIGH confidence — official docs)
- `https://github.com/erikd/libsndfile/blob/master/src/rf64.c` — RF64 ds64 chunk layout (HIGH confidence — reference implementation)
- `https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/wavenc.c` — RF64 write pattern, 28-byte ds64 payload, JUNK/ds64 interplay (HIGH confidence — authoritative reference)
- `https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf` — EBU Tech 3306 RF64 specification (official spec; PDF access limited but structure confirmed via implementations above)
- Existing codebase `src/TdmAnalyzer.cpp`, `src/TdmAnalyzerResults.cpp`, `src/TdmAnalyzerResults.h` — confirmed existing API usage (HIGH confidence — primary source)

---

*Stack research for: Saleae TDM Analyzer v1.4 SDK & Export Modernization*
*Researched: 2026-02-24*
