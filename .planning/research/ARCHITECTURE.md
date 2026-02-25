# Architecture Research

**Domain:** Saleae Logic 2 TDM Analyzer Plugin — v1.4 Feature Integration
**Researched:** 2026-02-24
**Confidence:** HIGH (all findings verified against actual source code and official SDK documentation)

## Milestone Scope

Four integration questions for the v1.4 milestone:
1. **RF64** — How does `PCMWaveFileHandler` change? New structs? Both header paths or replace?
2. **FrameV2** — Where do FrameV2 fields go in `TdmAnalyzer.cpp`? Is it post-`CommitResults` or a separate callback?
3. **SDK update** — What files change when bumping the `FetchContent` SHA? Any CMake changes?
4. **Sample rate check** — `SetSettingsFromInterfaces()` or `WorkerThread()` setup?

---

## Current State (Verified from Source)

Reading the source before prescribing changes revealed that several "new" features are already partially implemented.

**FrameV2 is already implemented** (`TdmAnalyzer.cpp` lines 299-337):
```cpp
FrameV2 frame_v2;
frame_v2.AddInteger( "channel", mResultsFrame.mType );
frame_v2.AddInteger( "data", adjusted_value );
frame_v2.AddString("errors", error_str);
frame_v2.AddString("warnings", warning_str);
frame_v2.AddInteger("frame #", mFrameNum);
mResults->AddFrameV2( frame_v2, "slot", mResultsFrame.mStartingSampleInclusive, mResultsFrame.mEndingSampleInclusive );
```
`UseFrameV2()` is called in the constructor at line 13. The member `mResultsFrameV2` declared in `TdmAnalyzer.h` line 40 is unused — a local `frame_v2` is used instead.

**The 4 GiB guard is already in place** (`TdmAnalyzerResults.cpp` lines 169-202): aborts WAV generation and writes a plain-text warning. This guard is what RF64 replaces.

**`PCMExtendedWaveFileHandler` is fully implemented** but commented out at the call site (line 210). It uses WAVE EXTENSIBLE format, not RF64.

**SDK is at latest** — confirmed that commit `114a3b8306e6a5008453546eda003db15b002027` (July 2023) is the current HEAD of the `master` branch. No newer commits exist as of 2026-02-24.

---

## Feature 1: RF64 WAV Support

### What RF64 Is

RF64 (EBU TECH 3306) replaces `RIFF` with `RF64` in the four-byte chunk identifier. The 32-bit size field at offset 4 is set to `0xFFFFFFFF` (sentinel). A `ds64` chunk is inserted immediately after the RIFF-like header and before `fmt `, containing 64-bit sizes. This enables files up to ~16 exabytes. Legacy WAV readers that encounter `0xFFFFFFFF` in the size field either reject the file cleanly or check for the `ds64` chunk per the spec.

### RF64 Byte Layout

```
Offset  Size  Field
------  ----  -----
0       4     Chunk ID: "RF64"
4       4     Chunk size: 0xFFFFFFFF (sentinel)
8       4     WAVE type: "WAVE"

--- ds64 chunk ---
12      4     Chunk ID: "ds64"
16      4     Chunk size: 28
20      8     RIFF size (U64): total file size minus 8
28      8     Data chunk size (U64): PCM data bytes
36      8     Sample count (U64): total audio frames (not per-channel samples)
44      4     Table length: 0

--- fmt chunk (identical to standard PCM WAV) ---
48      4     "fmt "
52      4     Chunk size: 16
56      2     Format tag: 0x0001 (PCM)
58      2     Num channels
60      4     Samples per sec
64      4     Bytes per sec
68      2     Block align
70      2     Bits per sample

--- data chunk ---
72      4     "data"
76      4     Data size: 0xFFFFFFFF (sentinel)
80      ...   PCM sample data
```

**Total header before data: 80 bytes** (vs 44 bytes for plain PCM WAV, same as WAVE EXTENSIBLE).

### New: `WaveRF64Header` Struct

Add to `TdmAnalyzerResults.h` after the existing `WavePCMExtendedHeader` block:

```cpp
#pragma scalar_storage_order little-endian
#pragma pack(push, 1)
typedef struct
{
    char mRf64CkId[4]   = {'R', 'F', '6', '4'};   // @ 0
    U32  mRf64CkSize    = 0xFFFFFFFF;               // @ 4  sentinel
    char mWaveId[4]     = {'W', 'A', 'V', 'E'};    // @ 8

    char mDs64CkId[4]   = {'d', 's', '6', '4'};   // @ 12
    U32  mDs64CkSize    = 28;                       // @ 16
    U64  mRiffSize64    = 0;                        // @ 20  total file - 8
    U64  mDataSize64    = 0;                        // @ 28  PCM data bytes
    U64  mSampleCount64 = 0;                        // @ 36  audio frame count
    U32  mTableLength   = 0;                        // @ 44

    char mFmtCkId[4]    = {'f', 'm', 't', ' '};   // @ 48
    U32  mFmtCkSize     = 16;                       // @ 52
    U16  mFormatTag     = 0x0001;                   // @ 56
    U16  mNumChannels   = 1;                        // @ 58
    U32  mSamplesPerSec = 48000;                    // @ 60
    U32  mBytesPerSec   = 96000;                    // @ 64
    U16  mBlockSizeBytes = 2;                       // @ 68
    U16  mBitsPerSample = 16;                       // @ 70

    char mDataCkId[4]   = {'d', 'a', 't', 'a'};   // @ 72
    U32  mDataCkSize    = 0xFFFFFFFF;               // @ 76  sentinel
    /* data */                                      // @ 80
} WaveRF64Header;
#pragma pack(pop)
#pragma scalar_storage_order default

static_assert( sizeof( WaveRF64Header ) == 80,
    "WaveRF64Header must be 80 bytes per RF64 spec. "
    "Check #pragma pack(1) and all field types." );
```

### New: `RF64WaveFileHandler` Class

Add to `TdmAnalyzerResults.h` (class declaration) and `TdmAnalyzerResults.cpp` (implementation). The interface is identical to `PCMWaveFileHandler` — same constructor signature, same `addSample()`, same `close()` — so the call site in `GenerateWAV()` changes only the type name.

Key implementation differences from `PCMWaveFileHandler`:

| Aspect | PCMWaveFileHandler | RF64WaveFileHandler |
|--------|-------------------|---------------------|
| Header struct | `WavePCMHeader` (44 bytes) | `WaveRF64Header` (80 bytes) |
| Size tracking types | `U32 mTotalFrames` | `U64 mTotalFrames` |
| `updateFileSize()` seeks | offsets 4, 40 (U32 writes) | offsets 20, 28, 36 (U64 writes) + offset 76 sentinel stays |
| Max file size | 4 GiB (RIFF limit) | ~16 exabytes |
| Periodic update target | `RIFF_CKSIZE_POS = 4`, `DATA_CKSIZE_POS = 40` | `DS64_RIFF_SIZE_POS = 20`, `DS64_DATA_SIZE_POS = 28`, `DS64_SAMPLE_COUNT_POS = 36` |

The `updateFileSize()` method uses 8-byte writes for ds64 fields. The existing `writeLittleEndianData(value, num_bytes)` helper already handles 8-byte writes when called with `num_bytes = 8`.

Class declaration to add to `TdmAnalyzerResults.h`:

```cpp
class RF64WaveFileHandler
{
  public:
    RF64WaveFileHandler( std::ofstream& file, U32 sample_rate = 48000,
                         U32 num_channels = 2, U32 bits_per_channel = 32 );
    ~RF64WaveFileHandler();
    void addSample( U64 sample );
    void close( void );

  private:
    void writeLittleEndianData( U64 value, U8 num_bytes );
    void updateFileSize();

  private:
    WaveRF64Header mWaveHeader;
    U32 mNumChannels;
    U32 mBitsPerChannel;
    U8  mBitShift;
    U32 mBytesPerChannel;
    U32 mSampleRate;
    U32 mFrameSizeBytes;
    U64 mTotalFrames;   // U64 — not U32
    U64 mSampleCount;   // U64 — not U32
    U32 mSampleIndex;
    std::ofstream& mFile;
    std::streampos mWtPosSaved;
    U64* mSampleData;

    constexpr static U64 DS64_RIFF_SIZE_POS    = 20;
    constexpr static U64 DS64_DATA_SIZE_POS    = 28;
    constexpr static U64 DS64_SAMPLE_COUNT_POS = 36;
};
```

### Modified: `GenerateWAV()` in `TdmAnalyzerResults.cpp`

Remove the entire 4 GiB pre-export guard block (lines 169-202). Replace `PCMWaveFileHandler` with `RF64WaveFileHandler`:

```cpp
void TdmAnalyzerResults::GenerateWAV( const char* file )
{
    U64 num_frames = GetNumFrames();
    // [4 GiB guard REMOVED — RF64 handles any file size]

    std::ofstream f;
    f.open( file, std::ios::out | std::ios::binary );

    if( f.is_open() )
    {
        RF64WaveFileHandler wave_file_handler( f,
            mSettings->mTdmFrameRate,
            mSettings->mSlotsPerFrame,
            mSettings->mDataBitsPerSlot );
        // ... loop body unchanged from existing PCMWaveFileHandler usage
    }
}
```

### What Happens to Existing Header Structs and Classes

- `WavePCMHeader` (44 bytes): Keep unchanged. `PCMWaveFileHandler` stays in the codebase as verified, working code. It is not called from `GenerateWAV()` after this change, but removing it is unnecessary churn and risks introducing regressions.
- `WavePCMExtendedHeader` (80 bytes): Keep unchanged. Already dead code (commented out at call site). RF64 supersedes its use case.
- `PCMExtendedWaveFileHandler`: Keep unchanged. Fully implemented, complete, no bugs. Future use case possible.
- Both `static_assert` size checks: Keep unchanged.

The `PCMExtendedWaveFileHandler` being 80 bytes (same as `WaveRF64Header`) is coincidence — the byte layout is entirely different.

---

## Feature 2: FrameV2 Data Enrichment

### Where the Code Goes

**`TdmAnalyzer.cpp`, function `AnalyzeTdmSlot()`, lines 299-337** — the existing FrameV2 block. This is the only correct location.

Do not move FrameV2 population elsewhere:
- Not to `WorkerThread()` — slot data values are not available there; only the frame loop structure lives there
- Not to a separate callback — the SDK has no post-frame callback; all `AddFrameV2()` calls must complete before `CommitResults()` at line 339
- Not to `TdmAnalyzerResults` — the results class has access only to `Frame.mFlags` (8 bits), not the raw slot number or per-frame error booleans needed for structured fields

### What the Existing FrameV2 Block Has (Correct, Keep)

- `"channel"` — 0-based slot type (from `mResultsFrame.mType`)
- `"data"` — the decoded sample value (signed or unsigned)
- `"errors"` — concatenated error string
- `"warnings"` — concatenated warning string
- `"frame #"` — TDM frame counter (with a space in the key name)

### What to Add or Change

Add per-error boolean fields for HLA machine-parseable access. Rename `"frame #"` to `"frame_number"` (Saleae convention: lowercase, underscore-separated, no spaces). Optionally add a 1-based `"slot"` field alongside the existing 0-based `"channel"` field for clarity.

Recommended final FrameV2 block:

```cpp
FrameV2 frame_v2;

// Identity
frame_v2.AddInteger( "channel", S64( mResultsFrame.mType ) );       // 0-based, kept for backward compat
frame_v2.AddInteger( "slot",    S64( mResultsFrame.mType + 1 ) );   // 1-based, human-readable
frame_v2.AddInteger( "frame_number", S64( mFrameNum ) );             // was "frame #"

// Decoded sample
S64 adjusted_value = result;
if( mSettings->mSigned == AnalyzerEnums::SignedInteger )
    adjusted_value = AnalyzerHelpers::ConvertToSignedNumber( mResultsFrame.mData1, mSettings->mDataBitsPerSlot );
frame_v2.AddInteger( "data", adjusted_value );

// Machine-parseable error booleans for HLA use
frame_v2.AddBoolean( "short_slot",        ( mResultsFrame.mFlags & SHORT_SLOT )        != 0 );
frame_v2.AddBoolean( "extra_slot",        ( mResultsFrame.mFlags & UNEXPECTED_BITS )   != 0 );
frame_v2.AddBoolean( "data_error",        ( mResultsFrame.mFlags & MISSED_DATA )       != 0 );
frame_v2.AddBoolean( "frame_sync_missed", ( mResultsFrame.mFlags & MISSED_FRAME_SYNC ) != 0 );
frame_v2.AddBoolean( "bitclock_error",    ( mResultsFrame.mFlags & BITCLOCK_ERROR )    != 0 );

// Human-readable summaries (kept for display)
frame_v2.AddString( "errors",   error_str );
frame_v2.AddString( "warnings", warning_str );

mResults->AddFrameV2( frame_v2, "slot",
    mResultsFrame.mStartingSampleInclusive,
    mResultsFrame.mEndingSampleInclusive );
```

### The Unused `mResultsFrameV2` Member

`TdmAnalyzer.h` line 40 declares `FrameV2 mResultsFrameV2;`. This member is never used — the implementation correctly uses a local `frame_v2` variable. A local variable is the right approach (FrameV2 is stack-allocated and populated fresh per slot). Remove `mResultsFrameV2` from `TdmAnalyzer.h` to eliminate dead state.

---

## Feature 3: SDK Audit and Update

### Audit Result: No Update Needed

Commit `114a3b8306e6a5008453546eda003db15b002027` **is the current HEAD of `master`**. Confirmed by fetching the GitHub commits page. No newer commits exist. The SDK update task for this milestone is a no-op in terms of code changes.

The audit question is: does the current code use the SDK correctly? Yes:
- `UseFrameV2()` called in constructor — correct
- `AddFrameV2()` called with string type `"slot"` before `CommitResults()` — correct
- Both `AddFrame()` and `AddFrameV2()` called — correct (V1 Frame still drives `GenerateBubbleText()`)
- `ClearTabularText()` as first call in `GenerateFrameTabularText()` — correct (was fixed in a prior milestone)
- `GetSampleRate()`, `GetMinimumSampleRateHz()`, `CheckIfThreadShouldExit()` — all used correctly

### If a Future SDK Commit Appears

Only one line changes: `cmake/ExternalAnalyzerSDK.cmake` line 21:
```cmake
GIT_TAG        114a3b8306e6a5008453546eda003db15b002027
```
Replace with the new full commit SHA. No other CMake changes are needed. After changing the SHA:
1. Delete the CMake build directory (FetchContent caches the old clone)
2. Rebuild on Windows, macOS x86_64, macOS arm64, and Linux
3. Inspect `AnalyzerSDKConfig.cmake` in the fetched source for new target properties or API additions
4. Grep for any deprecated call sites if the SDK changelog mentions breaking changes

`GIT_SHALLOW True` is correct to keep — it clones only that commit. Do not add `GIT_SUBMODULES` unless the SDK gains submodule dependencies.

---

## Feature 4: Sample Rate Sanity Check

### The Problem

`GetMinimumSampleRateHz()` (`TdmAnalyzer.cpp` line 363) correctly computes and returns the required minimum sample rate:
```cpp
return mSettings->mSlotsPerFrame * mSettings->mBitsPerSlot * mSettings->mTdmFrameRate * 4;
```

Logic 2 reads this value and displays a warning to the user when the capture sample rate is below this threshold. This runtime enforcement already exists and works. The gap is that Logic 2's warning is advisory — it does not prevent analysis from running with an undersized sample rate. More critically, no validation prevents the user from entering settings that produce a physically impossible minimum sample rate.

### Where the Check Goes: `SetSettingsFromInterfaces()`

**File:** `TdmAnalyzerSettings.cpp`
**Function:** `SetSettingsFromInterfaces()`

This is correct for configuration-level sanity: check that the combination of `mSlotsPerFrame`, `mBitsPerSlot`, and `mTdmFrameRate` does not require a sample rate that no Logic 2 hardware can provide.

Do not put the check in `WorkerThread()`. Rationale: `GetSampleRate()` (the actual Logic 2 capture rate) is available in `WorkerThread()`, but the per-slot calculations of `mDesiredBitClockPeriod` degrade silently rather than causing an obvious failure. The user should receive feedback at settings-entry time, not mid-analysis.

Do not put the check in `WorkerThread()` to call `GetSampleRate()` for comparison. The SDK's built-in `GetMinimumSampleRateHz()` mechanism already handles the runtime comparison — duplicating it in `WorkerThread()` adds complexity with no benefit.

### Validation Logic

```cpp
bool TdmAnalyzerSettings::SetSettingsFromInterfaces()
{
    // ... existing channel and parameter validation ...

    // Sample rate sanity check: compute minimum Logic 2 capture rate required.
    // Factor of 4 matches GetMinimumSampleRateHz() — four samples per bit edge.
    U64 required_sample_rate =
        U64( mSlotsPerFrame ) * U64( mBitsPerSlot ) * U64( mTdmFrameRate ) * 4ULL;

    // Logic 2 Pro supports up to 500 MSPS.
    // If the required rate exceeds hardware capability, the settings are physically unrealizable.
    constexpr U64 LOGIC2_MAX_SAMPLE_RATE_HZ = 500000000ULL;
    if ( required_sample_rate > LOGIC2_MAX_SAMPLE_RATE_HZ )
    {
        SetErrorText( "These TDM settings require a sample rate above 500 MSPS "
                      "(Logic 2 maximum). Reduce frame rate, slots per frame, "
                      "or bits per slot." );
        return false;
    }

    return true;
}
```

### Blocking vs. Warning

Return `false` only for physically impossible settings (required rate > 500 MSPS). Do not block settings that are merely marginal. For marginal cases, Logic 2's existing enforcement via `GetMinimumSampleRateHz()` provides the appropriate advisory warning at capture time.

If `SetWarningText()` or an equivalent soft-warning SDK API exists, use it for the marginal case (required rate > 100 MSPS, for example, which is achievable but unusual). Verify this API exists before relying on it.

### Integer Overflow Risk

`mSlotsPerFrame` (max 256), `mBitsPerSlot` (max 64), `mTdmFrameRate` (user-entered U32) — the product before the `* 4` factor can reach `256 * 64 * 48000 * 4 = 3,145,728,000` which fits in a U32. However if `mTdmFrameRate` can be set arbitrarily high (e.g., 1,000,000 Hz), the product overflows U32. Cast all operands to `U64` before multiplying, as shown above. The existing `GetMinimumSampleRateHz()` returns `U32` and is vulnerable to overflow at high frame rates — this is a pre-existing issue, fix the new check but do not need to fix the existing function in this task.

---

## Component Boundary Map

```
TdmAnalyzerSettings.cpp
  SetSettingsFromInterfaces()
    MODIFY: add sample rate sanity check with U64 arithmetic
            block physically impossible settings (> 500 MSPS required)

TdmAnalyzer.cpp
  TdmAnalyzer() constructor
    NO CHANGE: UseFrameV2() already called
  AnalyzeTdmSlot()
    MODIFY: FrameV2 block (lines 299-337)
      - add boolean error fields: short_slot, extra_slot, data_error,
        frame_sync_missed, bitclock_error
      - rename "frame #" to "frame_number"
      - keep "channel", "data", "errors", "warnings" unchanged

TdmAnalyzer.h
  MODIFY: remove unused mResultsFrameV2 member (line 40)

TdmAnalyzerResults.h
  WavePCMHeader            — NO CHANGE
  WavePCMExtendedHeader    — NO CHANGE
  PCMWaveFileHandler       — NO CHANGE
  PCMExtendedWaveFileHandler — NO CHANGE
  WaveRF64Header           — ADD (new struct, 80 bytes, with static_assert)
  RF64WaveFileHandler      — ADD (new class declaration)

TdmAnalyzerResults.cpp
  GenerateWAV()
    MODIFY: remove 4 GiB pre-export guard block (lines 169-202)
            replace PCMWaveFileHandler with RF64WaveFileHandler at line 209
  RF64WaveFileHandler implementation — ADD (~130 lines, mirrors PCMWaveFileHandler)

cmake/ExternalAnalyzerSDK.cmake
  GIT_TAG line — NO CHANGE (already at latest commit)
```

---

## Build Order

Features are independent; this sequence minimizes risk:

1. **FrameV2 enrichment** — In-place modification of existing `AnalyzeTdmSlot()` block. No new files, no header struct changes. Verify by loading in Logic 2 and inspecting the data table columns.

2. **Sample rate check** — Addition to `SetSettingsFromInterfaces()`. No new files, no header changes. Test by entering `mTdmFrameRate = 999999999` and confirming rejection; then enter a valid rate and confirming acceptance.

3. **RF64 WAV support** — New struct, new class, and `GenerateWAV()` modification. Most surface area. Test by exporting a small WAV (verify playback in Audacity), then exporting a capture known to exceed 4 GiB and confirming the RF64 file plays.

4. **SDK audit** — No code changes. Document audit completion in a commit message: "chore: SDK audit — 114a3b8 confirmed current HEAD, all APIs used correctly."

---

## Data Flow: Analysis Thread (FrameV2 Path)

```
WorkerThread() infinite loop
    |
    v
GetTdmFrame()
    |
    +-- for each completed slot's bits:
    v
AnalyzeTdmSlot()
    |
    +-- extract bits into result
    +-- mResults->AddFrame( mResultsFrame )        [V1 — drives bubble text]
    |
    +-- FrameV2 block (MODIFIED):
    |     frame_v2.AddInteger( "channel", ... )     [0-based, kept]
    |     frame_v2.AddInteger( "slot", ... )         [1-based, NEW]
    |     frame_v2.AddInteger( "frame_number", ... ) [renamed from "frame #"]
    |     frame_v2.AddInteger( "data", ... )         [unchanged]
    |     frame_v2.AddBoolean( "short_slot", ... )   [NEW]
    |     frame_v2.AddBoolean( "extra_slot", ... )   [NEW]
    |     frame_v2.AddBoolean( "data_error", ... )   [NEW]
    |     frame_v2.AddBoolean( "frame_sync_missed", ... ) [NEW]
    |     frame_v2.AddBoolean( "bitclock_error", ... )    [NEW]
    |     frame_v2.AddString( "errors", ... )        [unchanged]
    |     frame_v2.AddString( "warnings", ... )      [unchanged]
    |     mResults->AddFrameV2( frame_v2, "slot", start, end )
    |
    +-- mResults->CommitResults()     [must come after both AddFrame and AddFrameV2]
    +-- ReportProgress()
    +-- clear mDataBits, mDataValidEdges, mDataFlags
    +-- mSlotNum++
```

## Data Flow: RF64 Export Path

```
User clicks "Export to TXT/CSV" in Logic 2
    |
    v
TdmAnalyzerResults::GenerateExportFile()
    |
    +-- mSettings->mExportFileType == WAV
    |       |
    |       v
    |   GenerateWAV( file )
    |       |
    |       +-- [4 GiB guard REMOVED]
    |       +-- open std::ofstream in binary mode
    |       +-- RF64WaveFileHandler( f, mTdmFrameRate, mSlotsPerFrame, mDataBitsPerSlot )
    |       |       |
    |       |       +-- compute mBytesPerChannel, mBitShift, mFrameSizeBytes
    |       |       +-- populate WaveRF64Header fields
    |       |       +-- write 80-byte header to file (seekp(0) + write)
    |       |       +-- allocate mSampleData[mNumChannels]
    |       |
    |       +-- for each Frame i (0..num_frames-1):
    |       |       |
    |       |       +-- frame.mType < mSlotsPerFrame: addSample( sample_value )
    |       |               |
    |       |               +-- accumulate into mSampleData[mSampleIndex++]
    |       |               +-- when mSampleIndex == mNumChannels: flush audio frame
    |       |                       |
    |       |                       +-- mTotalFrames++ (U64)
    |       |                       +-- writeLittleEndianData() for each channel
    |       |                       +-- every (mSampleRate/100) frames: updateFileSize()
    |       |                               |
    |       |                               +-- seekp(DS64_RIFF_SIZE_POS=20), write U64
    |       |                               +-- seekp(DS64_DATA_SIZE_POS=28), write U64
    |       |                               +-- seekp(DS64_SAMPLE_COUNT_POS=36), write U64
    |       |
    |       +-- wave_file_handler.close()
    |               |
    |               +-- updateFileSize() (final accurate sizes)
    |               +-- pad to even byte boundary if needed
    |               +-- mFile.close()
    |
    +-- mSettings->mExportFileType == CSV
            |
            v
        GenerateCSV() — unchanged
```

---

## Anti-Patterns Specific to This Milestone

### Anti-Pattern 1: Keeping the 4 GiB Guard When Switching to RF64

**What happens:** Add `RF64WaveFileHandler` but leave the guard block in `GenerateWAV()`. The guard runs first. All captures exceeding 4 GiB produce a plain-text warning file. RF64 is never reached for large exports.

**Do this instead:** Remove the entire guard block (lines 169-202). RF64 has no 4 GiB limit. The guard is the old workaround — RF64 makes it obsolete.

### Anti-Pattern 2: U32 for Frame/Sample Counters in RF64WaveFileHandler

**What happens:** Copy `PCMWaveFileHandler` verbatim including `U32 mTotalFrames`. At ~4 billion audio frames (about 25 hours at 48 kHz stereo), the counter overflows silently. The ds64 sizes become wrong.

**Do this instead:** Use `U64 mTotalFrames` and `U64 mSampleCount` in `RF64WaveFileHandler`. The `writeLittleEndianData` helper writes N bytes; call it with `num_bytes = 8` for U64 ds64 fields.

### Anti-Pattern 3: Adding FrameV2 Fields After CommitResults

**What happens:** Add a second `frame_v2.AddInteger(...)` call after `mResults->CommitResults()` at line 339. The field is associated with an uncommitted frame object that never gets submitted to the SDK.

**Do this instead:** All `frame_v2.Add*()` calls and `mResults->AddFrameV2()` must appear before `CommitResults()`. The existing code structure is correct — do not restructure this ordering.

### Anti-Pattern 4: Blocking Marginal (Not Impossible) Sample Rate Settings

**What happens:** Return `false` from `SetSettingsFromInterfaces()` for any required sample rate above, say, 25 MSPS. This blocks users from analyzing real TDM signals at professional audio rates (3-12 Mbit/s TDM is common).

**Do this instead:** Only return `false` for required rates exceeding Logic 2's physical maximum (500 MSPS). For rates that are high but achievable, rely on Logic 2's existing enforcement via `GetMinimumSampleRateHz()`.

---

## Integration Points Table

| Feature | Files Modified | Components Added | SDK Dependency |
|---------|---------------|-----------------|----------------|
| RF64 WAV | `TdmAnalyzerResults.cpp` (`GenerateWAV`) | `WaveRF64Header` struct, `RF64WaveFileHandler` class (both in `TdmAnalyzerResults.h/.cpp`) | None beyond current SDK |
| FrameV2 enrichment | `TdmAnalyzer.cpp` (`AnalyzeTdmSlot`), `TdmAnalyzer.h` (remove unused member) | None | `FrameV2::AddBoolean()` — present in SDK at 114a3b8 |
| SDK update | `cmake/ExternalAnalyzerSDK.cmake` (`GIT_TAG` line) | None | N/A — no update available |
| Sample rate check | `TdmAnalyzerSettings.cpp` (`SetSettingsFromInterfaces`) | None | None |

---

## Sources

- Saleae AnalyzerSDK GitHub (master HEAD confirmed 114a3b8, 2026-02-24): [github.com/saleae/AnalyzerSDK](https://github.com/saleae/AnalyzerSDK)
- AnalyzerResults.h (FrameV2 class): [github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerResults.h](https://github.com/saleae/AnalyzerSDK/blob/master/include/AnalyzerResults.h)
- Saleae FrameV2 / HLA Support documentation: [support.saleae.com — FrameV2 HLA Support](https://support.saleae.com/product/user-guide/extensions-apis-and-sdks/saleae-api-and-sdk/protocol-analyzer-sdk/framev2-hla-support-analyzer-sdk)
- EBU TECH 3306 RF64 specification: [tech.ebu.ch/docs/tech/tech3306v1_1.pdf](https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf)
- RF64 Wikipedia overview: [en.wikipedia.org/wiki/RF64](https://en.wikipedia.org/wiki/RF64)
- PlayPcmWin RF64 structure reference: [sourceforge.net/p/playpcmwin/wiki/RF64 WAVE](https://sourceforge.net/p/playpcmwin/wiki/RF64%20WAVE/)
- Project source files verified: `src/TdmAnalyzer.cpp`, `src/TdmAnalyzer.h`, `src/TdmAnalyzerResults.h/.cpp`, `src/TdmAnalyzerSettings.h/.cpp`, `cmake/ExternalAnalyzerSDK.cmake`

---

*Architecture research for: Saleae Logic 2 TDM Analyzer Plugin v1.4 integration*
*Researched: 2026-02-24*
