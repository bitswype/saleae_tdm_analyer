# Phase 7: RF64 WAV Export - Research

**Researched:** 2026-02-25
**Domain:** RF64 audio file format, C++ binary file I/O, WAV chunk structure
**Confidence:** HIGH

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| RF64-01 | Create WaveRF64Header packed struct (80 bytes: RF64 root 12 + ds64 36 + fmt 20 + data header 8 + padding 4) with static_assert size guard | Byte layout verified from EBU TECH 3306 spec + FFmpeg source; struct fields and offsets documented in Code Examples section |
| RF64-02 | Create RF64WaveFileHandler class with U64 frame/sample counters and ds64 seek-back-at-close to write true sizes | Seek-back pattern verified from FFmpeg wavenc.c; mirrors existing PCMWaveFileHandler.updateFileSize() pattern with U64 counterparts |
| RF64-03 | Modify GenerateWAV to use RF64WaveFileHandler when estimated data exceeds 4 GiB, standard PCMWaveFileHandler otherwise | Threshold logic already exists in current 4 GiB guard; replace guard with conditional dispatch |
| RF64-04 | Remove existing 4 GiB text-error guard from GenerateWAV — replaced by conditional RF64 path | Existing guard code identified at TdmAnalyzerResults.cpp lines 170-202 |
</phase_requirements>

---

## Summary

RF64 is an EBU standard (TECH 3306) for WAV files exceeding 4 GiB. It replaces the RIFF chunk ID with `RF64`, sets the 32-bit RIFF chunk size field to the sentinel `0xFFFFFFFF`, inserts a mandatory `ds64` chunk immediately after the `WAVE` form type, and sets the `data` chunk's 32-bit size field to `0xFFFFFFFF` as well. The actual 64-bit sizes are written into the `ds64` chunk using a seek-back-at-close pattern identical to how `PCMWaveFileHandler::updateFileSize()` already works — except the fields are `U64` instead of `U32`.

The implementation is a single new class (`RF64WaveFileHandler`) and one new packed struct (`WaveRF64Header`), both in `TdmAnalyzerResults.h/.cpp`, mirroring the existing `PCMWaveFileHandler` pattern closely. The `GenerateWAV` method already computes an estimated data size to trigger the existing guard; that same estimate switches dispatch between `PCMWaveFileHandler` and `RF64WaveFileHandler`. The text-error guard is deleted outright.

The key design decision already locked in STATE.md: "always write RF64 headers directly (no JUNK-to-ds64 upgrade path)." This means no progressive upgrade — the struct contains `RF64` and sentinel values from byte 0, and the real sizes are written on `close()` via seekback.

**Primary recommendation:** Model `RF64WaveFileHandler` directly on `PCMWaveFileHandler`, replacing `U32 mTotalFrames` with `U64`, `U32 mSampleCount` with `U64`, all `updateFileSize()` writes with `U64` little-endian writes, and the header struct with `WaveRF64Header`. Keep the `addSample()`/`writeLittleEndianData()`/`close()` interface identical so `GenerateWAV` requires only a constructor-call substitution.

---

## Standard Stack

### Core
| Component | Version | Purpose | Why Standard |
|-----------|---------|---------|--------------|
| EBU TECH 3306 RF64 spec | v1.1 (2009) | Normative format definition | The canonical specification; Audacity, FFmpeg, and all major tools implement this spec |
| `std::ofstream` (C++11) | Already in use | File I/O with `seekp()` for seek-back updates | Already used by `PCMWaveFileHandler`; no new dependencies |
| `#pragma pack(push, 1)` + `static_assert` | GCC/Clang/MSVC | Packed struct with compile-time size guard | Already used for `WavePCMHeader` and `WavePCMExtendedHeader` in this file |
| `U64` (Saleae SDK type) | Already in use | 64-bit frame/sample counters | Required for >4 GiB counts; SDK typedef already available throughout codebase |

### Supporting
| Component | Purpose | When to Use |
|-----------|---------|-------------|
| `ffprobe` (FFmpeg CLI) | Verify exported RF64 file structure | Manual verification during development; reads `RF64` codec, channel count, sample rate |
| `xxd` or `hexdump` | Byte-level verification of header sentinel values and ds64 fields | Confirm `0xFFFFFFFF` at offsets 4 and 76; confirm `RF64` at offset 0 |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Direct `std::ofstream` seek-back | JUNK-chunk upgrade path (BWF pattern) | JUNK approach allows backward compat during recording but adds complexity. STATE.md locks in direct RF64 write — no JUNK approach |
| Standard PCM WAV always | Always-RF64 mode | Always-RF64 breaks older tools; conditional RF64 is locked in REQUIREMENTS.md Out of Scope |
| W64 (Sony) format | RF64 (EBU) | W64 has narrow tool adoption (FFmpeg only); RF64 is the EBU standard. REQUIREMENTS.md explicitly rules out W64 |

---

## Architecture Patterns

### Recommended Project Structure

No new files are needed. All additions go into existing files:
```
src/
├── TdmAnalyzerResults.h   — Add WaveRF64Header struct + RF64WaveFileHandler class declaration
└── TdmAnalyzerResults.cpp — Add RF64WaveFileHandler implementation + modify GenerateWAV
```

### Pattern 1: RF64 Fixed Header Struct (packed, little-endian, 80 bytes)

**What:** A `#pragma pack(push, 1)` struct that matches the EBU TECH 3306 RF64 binary layout exactly. Written once at file open; seeked back into for ds64 size fields at close.

**When to use:** At construction of `RF64WaveFileHandler`, written via `mFile.write((const char*)&mRF64Header, sizeof(mRF64Header))` — identical to the existing `WavePCMHeader` write.

**Exact byte layout (verified against EBU TECH 3306 and FFmpeg wavenc.c):**

```
Offset  Size  Field                      Value at open
------  ----  -------------------------  ---------------
0       4     RF64 root ckID             "RF64"
4       4     RIFF chunk size            0xFFFFFFFF (sentinel)
8       4     WAVE form type             "WAVE"
--- RF64 root: 12 bytes ---
12      4     ds64 chunk ckID            "ds64"
16      4     ds64 chunk size            28 (U32, fixed)
20      8     riffSize (U64)             0 at open; written at close
28      8     dataSize (U64)             0 at open; written at close
36      8     sampleCount (U64)          0 at open; written at close
44      4     tableLength (U32)          0 (no extra chunk table entries)
--- ds64 chunk: 36 bytes (ckID 4 + ckSize 4 + payload 28) ---
48      4     fmt chunk ckID             "fmt "
52      4     fmt chunk size             16 (U32, standard PCM)
56      2     formatTag (U16)            0x0001 (PCM)
58      2     numChannels (U16)          from settings
60      4     sampleRate (U32)           from settings
64      4     byteRate (U32)             frameSize * sampleRate
68      2     blockAlign (U16)           frameSize (bytes per multi-ch frame)
70      2     bitsPerSample (U16)        from settings (rounded up to byte boundary)
--- fmt chunk: 24 bytes ---
72      4     data chunk ckID            "data"
76      4     data chunk size            0xFFFFFFFF (sentinel)
--- data chunk header: 8 bytes ---
TOTAL: 80 bytes
```

`static_assert(sizeof(WaveRF64Header) == 80, ...)` — matches REQUIREMENTS.md RF64-01.

**C++ struct declaration:**

```cpp
// Source: EBU TECH 3306 v1.1; byte offsets verified against FFmpeg libavformat/wavenc.c
#pragma scalar_storage_order little-endian
#pragma pack(push, 1)
typedef struct
{
    // RF64 root chunk (12 bytes, offsets 0-11)
    char  mRF64CkId[4]   = {'R', 'F', '6', '4'};  // @ 0
    U32   mRiffCkSize    = 0xFFFFFFFF;              // @ 4  sentinel
    char  mWaveId[4]     = {'W', 'A', 'V', 'E'};   // @ 8

    // ds64 chunk (36 bytes, offsets 12-47)
    char  mDs64CkId[4]   = {'d', 's', '6', '4'};   // @ 12
    U32   mDs64CkSize    = 28;                      // @ 16  payload size (3×U64 + 1×U32)
    U64   mRiffSize      = 0;                       // @ 20  written at close()
    U64   mDataSize      = 0;                       // @ 28  written at close()
    U64   mSampleCount   = 0;                       // @ 36  written at close()
    U32   mTableLength   = 0;                       // @ 44  no extra entries

    // fmt chunk (24 bytes, offsets 48-71)
    char  mFmtCkId[4]    = {'f', 'm', 't', ' '};   // @ 48
    U32   mFmtCkSize     = 16;                      // @ 52  standard PCM
    U16   mFormatTag     = 0x0001;                  // @ 56  PCM
    U16   mNumChannels   = 1;                       // @ 58
    U32   mSamplesPerSec = 8000;                    // @ 60
    U32   mBytesPerSec   = 16000;                   // @ 64
    U16   mBlockSizeBytes= 2;                       // @ 68
    U16   mBitsPerSample = 16;                      // @ 70

    // data chunk header (8 bytes, offsets 72-79)
    char  mDataCkId[4]   = {'d', 'a', 't', 'a'};   // @ 72
    U32   mDataCkSize    = 0xFFFFFFFF;              // @ 76  sentinel
    /* audio data starts @ 80 */
} WaveRF64Header;
#pragma pack(pop)
#pragma scalar_storage_order default

static_assert(sizeof(WaveRF64Header) == 80,
    "WaveRF64Header must be exactly 80 bytes per RF64 spec. "
    "Check #pragma pack(1) and field types.");
```

### Pattern 2: Seek-Back at Close (RF64 64-bit sizes)

**What:** At `close()`, seek to the three ds64 size fields and the data chunk header and write the real 64-bit values. Also write the 32-bit RIFF chunk size at offset 4 — but since this is always >4 GiB when RF64 is used, that field stays as `0xFFFFFFFF` (sentinel). The only fields that change are the U64s in ds64.

**Key offsets for seekback (compile-time constants):**
```cpp
constexpr static U64 RF64_DS64_RIFFSIZE_POS  = 20;  // U64 riffSize in ds64
constexpr static U64 RF64_DS64_DATASIZE_POS  = 28;  // U64 dataSize in ds64
constexpr static U64 RF64_DS64_SAMPLECNT_POS = 36;  // U64 sampleCount in ds64
// data ckSize at offset 76 stays 0xFFFFFFFF — sentinel, not updated
```

**How values are computed at close():**
```cpp
// Source: FFmpeg wavenc.c ds64 write pattern + EBU TECH 3306 §5.4
U64 dataSizeBytes = mTotalFrames * mFrameSizeBytes;
// Pad to even byte boundary (WAV requirement)
if ((dataSizeBytes % 2) == 1) dataSizeBytes += 1;

// riffSize = total file bytes minus 8 (the RF64 ckID + sentinel ckSize fields)
// file size = 80 (header) + dataSizeBytes
U64 riffSize = 80 - 8 + dataSizeBytes;  // = 72 + dataSizeBytes

// dataSize = actual data chunk payload bytes (without the data ckID + ckSize header)
U64 dataSize = dataSizeBytes;

// sampleCount = total individual samples across all channels
U64 sampleCount = mTotalFrames * mNumChannels;
```

**Seekback write sequence:**
```cpp
void RF64WaveFileHandler::updateFileSize(void)
{
    std::streampos savedPos = mFile.tellp();

    U64 dataSizeBytes = mTotalFrames * mFrameSizeBytes;
    if ((dataSizeBytes % 2) == 1) dataSizeBytes += 1;

    U64 riffSize    = 72ULL + dataSizeBytes;      // 80 - 8 = 72
    U64 dataSize    = dataSizeBytes;
    U64 sampleCount = mTotalFrames * (U64)mNumChannels;

    mFile.seekp(RF64_DS64_RIFFSIZE_POS);
    writeLittleEndianData(riffSize,    8);

    mFile.seekp(RF64_DS64_DATASIZE_POS);
    writeLittleEndianData(dataSize,    8);

    mFile.seekp(RF64_DS64_SAMPLECNT_POS);
    writeLittleEndianData(sampleCount, 8);

    mFile.seekp(savedPos);
}
```

### Pattern 3: GenerateWAV Conditional Dispatch

**What:** The existing estimate logic (lines 170-186 of TdmAnalyzerResults.cpp) already computes `estimated_data_bytes`. Use that same estimate to choose the handler. The threshold is the same `WAV_MAX_DATA_BYTES` constant.

```cpp
void TdmAnalyzerResults::GenerateWAV( const char* file )
{
    // ... existing bytes-per-channel + frame-size computation unchanged ...

    constexpr U64 WAV_MAX_DATA_BYTES = (U64)0xFFFFFFFF - 36ULL;
    U64 estimated_data_bytes = num_frames * frame_size_bytes;

    std::ofstream f;
    f.open(file, std::ios::out | std::ios::binary);

    if (f.is_open())
    {
        if (estimated_data_bytes > WAV_MAX_DATA_BYTES)
        {
            RF64WaveFileHandler wave_file_handler(f, mSettings->mTdmFrameRate,
                                                  mSettings->mSlotsPerFrame,
                                                  mSettings->mDataBitsPerSlot);
            // ... same loop as now ...
            wave_file_handler.close();
        }
        else
        {
            PCMWaveFileHandler wave_file_handler(f, mSettings->mTdmFrameRate,
                                                 mSettings->mSlotsPerFrame,
                                                 mSettings->mDataBitsPerSlot);
            // ... same loop as now ...
            wave_file_handler.close();
        }
    }
}
```

The text-error guard block (lines 188-202) is deleted entirely (RF64-04).

### Anti-Patterns to Avoid

- **U32 overflow in frame/sample counters:** The existing `PCMWaveFileHandler` uses `U32 mTotalFrames` and `U32 mSampleCount`. RF64WaveFileHandler MUST use `U64` for both — a >4 GiB file at 4 bytes/channel × 32 channels × 48kHz is ~7M frames, which fits U32, but at 8 bytes/channel × 32 channels × 192kHz it can overflow. Use U64 unconditionally.
- **Writing the RIFF 32-bit sentinel on seekback:** Do NOT overwrite the sentinel at offset 4. It must stay `0xFFFFFFFF`. Only ds64 U64 fields are updated at close.
- **Forgetting the odd-byte pad:** The existing `close()` writes a zero pad byte if data length is odd — RF64WaveFileHandler must do the same.
- **Using `int` or `long` for offsets:** `mFile.seekp()` takes `std::streamoff`; on Linux 64-bit this is 64-bit, but use explicit `U64` constants to avoid any ambiguity.
- **Omitting `#pragma scalar_storage_order little-endian`:** The existing structs use this GCC pragma. Include it for consistency even though it's ignored by Clang/MSVC (the `static_assert` catches packing errors on all compilers).

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| RF64 format parsing/reading | Custom parser | Not needed — this is write-only | Only exporting; no need to read back |
| External library for RF64 write | libsndfile, libbw64, NAudio | Direct `std::ofstream` + packed struct | No external deps available in this SDK plugin context; the format is simple enough for direct write |
| 64-bit little-endian serialization | Custom bit-manipulation | `writeLittleEndianData(value, 8)` — already exists in `PCMWaveFileHandler` | Copy/reuse the exact same helper function |

**Key insight:** The RF64 write path is structurally identical to the existing PCM write path. The only differences are: (1) the header struct, (2) U64 counters instead of U32, and (3) three U64 seekback writes instead of two U32 seekback writes. Everything else — `addSample()`, bit shifting, 8-bit offset encoding, even-byte padding — is a direct copy.

---

## Common Pitfalls

### Pitfall 1: ds64 chunk size field value
**What goes wrong:** Writing `ds64` with wrong chunk size. The `mDs64CkSize` field (at offset 16) holds the size of the `ds64` payload — NOT including the `ckID` (4 bytes) and `ckSize` (4 bytes) fields themselves. It is always `28` (3 × 8-byte U64 + 1 × 4-byte U32 = 28), regardless of the audio data size.
**Why it happens:** Confusing "chunk payload size" with "total chunk bytes on disk."
**How to avoid:** Hard-code `mDs64CkSize = 28` in the struct default initializer. The static_assert will catch if the struct layout drifts.
**Warning signs:** FFprobe reports ds64 chunk as malformed; Audacity refuses to open the file.

### Pitfall 2: sampleCount definition in ds64
**What goes wrong:** Writing `sampleCount` as frames-per-channel rather than total samples.
**Why it happens:** "Sample count" is ambiguous — it means total individual samples (frames × channels) per the EBU spec, equivalent to what the PCMExtendedWaveFileHandler writes into the `mSampleLength` field of the `fact` chunk: `mNumChannels * mTotalFrames`.
**How to avoid:** `sampleCount = mTotalFrames * mNumChannels;`
**Warning signs:** Audacity shows wrong duration; ffprobe shows incorrect sample count.

### Pitfall 3: riffSize calculation
**What goes wrong:** Off-by-8 error in riffSize.
**Why it happens:** riffSize = "size of the entire file minus 8 bytes." The "minus 8" excludes the RF64 ckID (4 bytes) and the RIFF chunk size field (4 bytes) — exactly as in standard RIFF. File total = 80 (header) + dataSizeBytes. So riffSize = 80 - 8 + dataSizeBytes = 72 + dataSizeBytes.
**How to avoid:** Use the formula `72ULL + dataSizeBytes` (where 72 = 80 − 8). Cross-check: for a 0-byte data file, riffSize should be 72 (accounts for fmt, ds64, data header but not the outer RF64+size fields).
**Warning signs:** ffprobe reports "invalid RIFF size" or truncated file.

### Pitfall 4: U32 overflow in mTotalFrames during interim updateFileSize calls
**What goes wrong:** The existing `PCMWaveFileHandler::addSample()` calls `updateFileSize()` periodically — every `mSampleRate / 100` frames. RF64WaveFileHandler does the same for progress. If `mTotalFrames` is `U32`, it wraps at ~4 billion frames. For a 2-channel 24-bit 48kHz stream, this is ~24.8 hours. For 32-channel 48kHz it's ~0.8 hours — easily reachable in a long logic capture.
**How to avoid:** Use `U64 mTotalFrames` in RF64WaveFileHandler.
**Warning signs:** File suddenly has corrupted ds64 size values mid-export; file size grows past expected value.

### Pitfall 5: Forgetting to open file in binary mode
**What goes wrong:** The `std::ofstream` opened without `std::ios::binary` translates `\n` to `\r\n` on Windows, corrupting audio data.
**Why it happens:** Not relevant on Linux, but the plugin runs on Windows too.
**How to avoid:** `f.open(file, std::ios::out | std::ios::binary)` — already present in `GenerateWAV`.
**Warning signs:** File size is larger than expected; Audacity reports corrupted audio on Windows.

---

## Code Examples

Verified patterns from EBU TECH 3306, FFmpeg wavenc.c analysis, and existing codebase conventions:

### Complete RF64WaveFileHandler Interface (matches PCMWaveFileHandler)

```cpp
// Mirrors PCMWaveFileHandler exactly; adds U64 counters and RF64 header
class RF64WaveFileHandler
{
  public:
    RF64WaveFileHandler(std::ofstream& file, U32 sample_rate, U32 num_channels, U32 bits_per_channel);
    ~RF64WaveFileHandler();

    void addSample(U64 sample);   // identical signature to PCMWaveFileHandler
    void close(void);

  private:
    void writeLittleEndianData(U64 value, U8 num_bytes);  // identical implementation
    void updateFileSize();         // seekback to ds64; writes three U64 fields

  private:
    WaveRF64Header mRF64Header;
    U32  mNumChannels;
    U32  mBitsPerChannel;
    U8   mBitShift;
    U32  mBytesPerChannel;
    U32  mSampleRate;
    U32  mFrameSizeBytes;
    U64  mTotalFrames;    // U64 (was U32 in PCMWaveFileHandler)
    U64  mSampleCount;    // U64 (was U32 in PCMWaveFileHandler)
    std::ofstream& mFile;
    std::streampos mWtPosSaved;
    U64* mSampleData;
    U32  mSampleIndex;

    // Byte offsets for seekback (verified from struct layout above)
    constexpr static U64 RF64_DS64_RIFFSIZE_POS  = 20;
    constexpr static U64 RF64_DS64_DATASIZE_POS  = 28;
    constexpr static U64 RF64_DS64_SAMPLECNT_POS = 36;
    // data ckSize at 76 stays 0xFFFFFFFF — not updated
};
```

### FFmpeg RF64 Write Pattern (reference implementation)

```c
// Source: FFmpeg libavformat/wavenc.c (master branch, Feb 2026)
// This is how FFmpeg writes the ds64 chunk on close:
avio_seek(pb, wav->ds64 - 8, SEEK_SET);
ffio_wfourcc(pb, "ds64");
avio_wl32(pb, 28);               // ds64 chunk payload size (always 28)
avio_wl64(pb, file_size - 8);   // riffSize
avio_wl64(pb, data_size);       // dataSize
avio_wl64(pb, number_of_samples); // sampleCount
avio_wl32(pb, 0);               // tableLength (no extra entries)
```

### Hex Dump Verification Pattern

After export, verify with `xxd` or `hexdump`:
```
Offset 0x00: 52 46 36 34  = "RF64"
Offset 0x04: FF FF FF FF  = 0xFFFFFFFF (RIFF chunk size sentinel)
Offset 0x08: 57 41 56 45  = "WAVE"
Offset 0x0C: 64 73 36 34  = "ds64"
Offset 0x10: 1C 00 00 00  = 28 (ds64 payload size)
Offset 0x14: [8 bytes]    = riffSize (little-endian U64)
Offset 0x1C: [8 bytes]    = dataSize (little-endian U64)
Offset 0x24: [8 bytes]    = sampleCount (little-endian U64)
Offset 0x2C: 00 00 00 00  = tableLength = 0
Offset 0x30: 66 6D 74 20  = "fmt "
...
Offset 0x48: 64 61 74 61  = "data"
Offset 0x4C: FF FF FF FF  = 0xFFFFFFFF (data chunk size sentinel)
```

### ffprobe Verification Command

```bash
ffprobe -v quiet -print_format json -show_streams export.wav
# Expected: codec_name "pcm_s16le" (or appropriate depth),
#           channels matching mSlotsPerFrame,
#           sample_rate matching mTdmFrameRate
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| JUNK-to-ds64 upgrade (BWF recording pattern) | Direct RF64 write from open | Locked in STATE.md before Phase 7 | Simpler implementation; no dual-format header juggling |
| 4 GiB text-error file at .wav path | Conditional RF64 handler | Phase 7 | Eliminates silent failure; large exports now succeed |
| EBU TECH 3306 v1.0 (2007) | EBU TECH 3306 v1.1 (MBWF/RF64, 2009) | 2009 | Adds multi-channel BWF metadata; the core ds64/sentinel mechanism is unchanged. Our implementation targets the core RF64 spec, not the MBWF extension |

**Not deprecated, but not used here:**
- MBWF `<axml>` and `<chna>` chunks: BWF broadcast metadata. Out of scope — this is a plain PCM audio export, not a broadcast delivery format.
- W64 (Sony/Sonic Foundry): Explicitly ruled out in REQUIREMENTS.md Out of Scope.
- Always-RF64 mode: Explicitly ruled out in REQUIREMENTS.md Out of Scope.

---

## Open Questions

1. **Does the `data` chunk 32-bit size sentinel (0xFFFFFFFF) cause any compatibility issues with Audacity or FFmpeg?**
   - What we know: EBU TECH 3306 spec says to set it to 0xFFFFFFFF for >4 GiB; FFmpeg reads it this way; this is standard.
   - What's unclear: Some older decoders may not handle it. Audacity 3.x is known to support RF64 per their release notes.
   - Recommendation: Use 0xFFFFFFFF as the sentinel per spec. Audacity compatibility is a Phase 7 success criterion — test manually.

2. **Should `updateFileSize()` be called periodically (as PCMWaveFileHandler does every `sampleRate/100` frames)?**
   - What we know: PCMWaveFileHandler calls it every 100ms worth of frames to keep the file recoverable if export is interrupted. The same logic applies to RF64.
   - What's unclear: Whether the periodic interval needs tuning for RF64 given larger files.
   - Recommendation: Replicate the same `mSampleRate / 100` interval — identical behavior to the PCM path.

3. **exact interpretation of "fmt 20" in RF64-01 requirement breakdown**
   - What we know: The requirement says "RF64 root 12 + ds64 36 + fmt 20 + data header 8 + padding 4 = 80." The actual fmt chunk is 24 bytes (4 ckID + 4 ckSize + 16 payload). 12+36+24+8 = 80 also equals 80 with no "padding 4."
   - What's unclear: The "fmt 20 + padding 4" decomposition — it may count the fmt chunk payload+ID as 20 and the ckSize field as "padding 4", or it may be a slightly informal count.
   - Recommendation: The `static_assert(sizeof(WaveRF64Header) == 80)` is the authoritative guard. The breakdown annotation in the struct comment should clarify the actual field sizes regardless of how the requirement phrases it.

---

## Sources

### Primary (HIGH confidence)
- EBU TECH 3306 v1.1 PDF (https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf) — normative RF64 specification; ds64 field definitions
- FFmpeg libavformat/wavenc.c (https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/wavenc.c) — production RF64 write implementation; seekback pattern and ds64 field order verified
- Existing codebase (`TdmAnalyzerResults.h`, `TdmAnalyzerResults.cpp`) — `PCMWaveFileHandler` architecture, seek-back pattern, `writeLittleEndianData`, field offsets for `WavePCMHeader`

### Secondary (MEDIUM confidence)
- Wikipedia RF64 article (https://en.wikipedia.org/wiki/RF64) — confirmed sentinel values, ds64 field summary
- WebSearch synthesis of EBU spec + FFmpeg source — confirmed "ds64 payload = 28 bytes", sentinel 0xFFFFFFFF at both RIFF and data ckSize, riffSize = file_size - 8

### Tertiary (LOW confidence)
- NAudio RF64 blog post (https://markheath.net/post/naudio-rf64-bwf) — C# reference implementation; implementation details match FFmpeg but not directly verified against EBU spec

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — format spec is stable (2009), FFmpeg implementation verified
- Architecture: HIGH — direct mirror of existing `PCMWaveFileHandler`; no novel patterns
- Pitfalls: MEDIUM — ds64 field size and sampleCount semantics verified from spec; riffSize formula cross-checked; U32 overflow pitfall is analytically derived

**Research date:** 2026-02-25
**Valid until:** 2027-02-25 (format spec is stable; no library version concerns)
