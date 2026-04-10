# TDM WAV Export - Real-Time WAV File HLA

A Saleae Logic 2 High Level Analyzer (HLA) that exports selected TDM slots to
a WAV file in real time during capture.

This is separate from the LLA's built-in CSV/WAV export. The HLA approach
offers slot selection and writes the WAV incrementally as data arrives, rather
than requiring a post-capture export step.

## Quick Start

1. In Logic 2, open the **Extensions** panel (right sidebar).
2. Click the **three-dots** menu icon → **"Load Existing Extension..."**.
3. Navigate to the `hla-wav-export/` folder and select `extension.json`.
4. Add **"TDM WAV Export"** to your analyzer chain after the **TdmAnalyzer** LLA.
5. Configure the settings (slots, output path, bit depth).
6. Start capturing - the WAV file is written as frames arrive.

## Settings

| Setting | Description | Default | Example |
|---------|-------------|---------|---------|
| **Slots** | Slot indices to export as WAV channels (comma-separated or ranges) | - | `0,2` or `0-3` or `1,3-5,7` |
| **Output Path** | **Absolute** path to the output `.wav` file | - | `/home/user/captures/output.wav` |
| **Bit Depth** | Sample bit depth (ignored in Audio Batch Mode - LLA bit depth is used) | `16` | `32` |

### Absolute Paths Required

The **Output Path** setting must be an absolute path. Relative paths resolve
against the Logic 2 application directory, not your working directory.

- **Linux:** `/home/user/captures/output.wav`
- **Windows:** `C:\Users\user\captures\output.wav`
- **macOS:** `/Users/user/captures/output.wav`

## How It Works

- The HLA reads FrameV2 slot frames from the upstream TdmAnalyzer LLA.
- Only the slots listed in **Slots** are written - others are discarded.
- Channels appear in the WAV in the order slots were specified, not sorted
  ascending. Specifying `4,2,0` produces a 3-channel WAV with slot 4 as
  channel 1, slot 2 as channel 2, and slot 0 as channel 3.
- The sample rate is derived automatically from the timing of decoded frames
  (same mechanism as the Audio Stream HLA).
- The WAV header is updated after every frame, so partial captures are
  playable - if you stop capture early, the WAV file is still valid.
- LLA error frames (short slot, bitclock error) produce silence in the WAV
  rather than crashing or corrupting the output.
- If **Output Path** is empty or **Slots** is invalid, the HLA emits a readable
  error in the Logic 2 protocol table rather than silently failing.

## Slot Specification Syntax

The **Slots** field supports:

| Format | Meaning | Example |
|--------|---------|---------|
| Single index | One slot | `0` |
| Comma-separated | Multiple slots | `0,2,4` |
| Range | Inclusive range | `0-3` (expands to 0,1,2,3) |
| Mixed | Combined | `0,2-4,7` (expands to 0,2,3,4,7) |

Duplicates are removed while preserving insertion order.

## Output Format

- **Standard PCM WAV** using Python's `wave` module.
- Output bit depth is 16 or 32 as selected in the HLA settings (no automatic
  tiering based on data bits).
- Data values are sign-extended to the output bit depth and written as-is
  (no scaling is applied).

## Comparison with LLA WAV Export

The LLA (C++ analyzer) also supports WAV export through the "Export to TXT/CSV"
action. The key differences:

| Feature | LLA Export | HLA (this tool) |
|---------|-----------|-----------------|
| When it runs | Post-capture export step | Real-time during capture |
| Slot selection | All slots (channel count from settings) | User-selected slots only |
| Channel ordering | Sequential | User-specified order |
| Partial capture | Must complete export | WAV valid at any point |
| RF64 support | Yes (>4 GiB) | No (standard PCM WAV only) |
| Configuration | Analyzer settings dropdown | HLA settings panel |

Use the HLA when you want real-time output, slot selection, or custom channel
ordering. Use the LLA export when you need a one-shot export of all channels
after capture.

## Error Handling

- **Init errors** (bad slot spec, missing path) are stored and emitted as an
  `AnalyzerFrame('error', ...)` on the first `decode()` call. This shows the
  error in the Logic 2 protocol table.
- **LLA error frames** (short slot, bitclock error) produce zero-value samples
  rather than propagating corrupted data.
- The **deferred-error pattern** ensures that `__init__` never raises - Logic 2
  would silently swallow the exception, leaving the user with no feedback.

## Architecture

```
TdmAnalyzer LLA
    │ FrameV2 slot frames
    ▼
TdmWavExport HLA (decode)
    ├── _try_derive_sample_rate()  - measures inter-frame timing
    ├── _try_flush()               - flush-before-accumulate boundary detection
    ├── _accum[slot] = sample      - accumulate current frame's data
    │         │
    │         ▼ (on frame boundary)
    │    _flush_frame()
    │         │
    │         ▼
    │    wave.writeframes() → output.wav
    │
    └── Returns None (no protocol table output for normal frames)
```

### Key Design Patterns

- **Settings at class level** - Logic 2 injects setting values before `__init__`
  runs. Settings are declared as class attributes, not constructor parameters.
- **`ChoicesSetting.default`** must be set as a separate statement after
  declaration, not as a kwarg - passing `default=` raises TypeError.
- **Flush-before-accumulate** - `_try_flush(frame_num)` is called before
  accumulating the new slot's data, ensuring the flush reads the previous
  frame's clean accumulator.
- **`try/except ImportError`** guard around `saleae.analyzers` enables running
  the module outside Logic 2 for self-testing.

## Self-Test

The module includes a self-test that runs outside Logic 2:

```bash
cd hla-wav-export && python TdmWavExport.py
```

This verifies slot parsing, signed conversion, sample rate derivation, and
WAV output.

## Known Limitations

### Looping (rolling) capture mode

This HLA will error out when used with Logic 2's looping capture mode. See the
[Known Limitations section](../README.md#known-limitations) in the main README
for details and workarounds.

## License

Licensed under the Apache License 2.0 - see [LICENSE](../LICENSE) for details.
