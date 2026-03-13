# CLAUDE.md — TDM Analyzer

## Overview

Saleae Logic 2 analyzer plugin for decoding TDM (Time Division Multiplexing) audio data. Three layers:

1. **C++ LLA** (`src/`) — Low Level Analyzer plugin, decodes raw signals into FrameV2 slot frames
2. **Python HLA: WAV Export** (`hla/`) — writes selected slots to a WAV file during capture
3. **Python HLA: Audio Stream** (`hla-audio-stream/`) — streams selected slots as live PCM over TCP

Plus two companion Python CLI tools in `tools/`.

## Project Structure

```
src/                          C++ LLA plugin (CMake build)
  TdmAnalyzer.cpp/h           Main analyzer — WorkerThread decodes TDM frames
  TdmAnalyzerSettings.cpp/h   User-configurable settings
  TdmAnalyzerResults.cpp/h    FrameV2 output (10 fields per slot frame)
  TdmSimulationDataGenerator.* Simulation data for Logic 2

hla/                          Python HLA — WAV export
  TdmWavExport.py             Writes selected slots to WAV file
  extension.json              Logic 2 extension manifest

hla-audio-stream/             Python HLA — live audio streaming
  TdmAudioStream.py           TCP server streaming PCM to companion CLI
  _tdm_utils.py               Shared utilities (parse_slot_spec, _as_signed)
  extension.json              Logic 2 extension manifest

tools/tdm-test-harness/       Standalone test harness (no Logic 2 needed)
  tdm_test_harness/cli.py     Click CLI: serve, verify, capture, analyze, quality-sweep, signals
  tdm_test_harness/signals.py Signal generators (sine, silence, ramp, WAV)
  tdm_test_harness/frame_emitter.py  Converts samples to fake AnalyzerFrames
  tdm_test_harness/hla_driver.py     Drives HLA outside Logic 2
  tdm_test_harness/verifier.py       TCP client for automated verification

tools/tdm-audio-bridge/       Companion CLI — plays streamed audio
  tdm_audio_bridge/cli.py     Click CLI: listen, devices
  tdm_audio_bridge/client.py  Auto-reconnecting TCP client
  tdm_audio_bridge/player.py  sounddevice playback engine
  tdm_audio_bridge/protocol.py  Handshake parsing and PCM unpacking
```

## Build

### C++ LLA (CMake)

```bash
# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# macOS (arm64)
cmake -B build/arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/arm64

# Windows (from Developer Command Prompt)
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build/Analyzers/libtdm_analyzer.so` (Linux/macOS) or `build\Analyzers\Release\tdm_analyzer.dll` (Windows).

### Python tools

```bash
pip install tools/tdm-test-harness/
pip install tools/tdm-audio-bridge/  # requires PortAudio
```

## Testing

Run test harness verification (no Logic 2 or hardware needed):

```bash
# Basic stereo sine test
tdm-test-harness verify --signal sine:440 --duration 0.5 --json

# Multi-channel
tdm-test-harness verify --signal sine:440,880,1320,1760 --channels 4 --json

# 32-bit
tdm-test-harness verify --signal ramp --bit-depth 32 --json
```

Exit code 0 = pass, 1 = fail. `--json` flag outputs structured results for automation.

### Audio quality analysis (requires sox)

```bash
# Capture TCP stream to WAV (while serve is running)
tdm-test-harness capture --port 4011 --duration 3 --skip 0.5 -o test.wav

# Analyze for glitches, dropouts, and frequency accuracy
tdm-test-harness analyze test.wav --freq 440

# Full automated quality sweep across 9 configurations
tdm-test-harness quality-sweep
```

The `analyze` command uses sox to:
- Verify the dominant frequency matches expected
- Detect glitches via notch filter + windowed RMS (50ms windows, -40 dB threshold)
- Detect dropouts via silence removal duration comparison

## Critical Patterns

### Logic 2 HLA conventions

- **Settings at class level** — Logic 2 injects values before `__init__` runs
- `ChoicesSetting.default` must be set as a separate statement, not a kwarg
- **Deferred error** — `__init__` wraps in try/except, stores in `self._init_error`, `decode()` emits error frame once on first call
- **Flush-before-accumulate** — `_try_flush(frame_num)` MUST be called before `self._accum[slot] = sample`
- `try/except ImportError` guard around `saleae.analyzers` enables running outside Logic 2
- `decode()` returns `None` for normal operation

### TCP protocol (audio stream)

- JSON handshake line (newline-terminated) with: protocol, sample_rate, channels, bit_depth, slot_list, buffer_size, byte_order
- Then raw interleaved little-endian PCM (int16 or int32)
- Ring buffer (deque with maxlen) drops oldest frames on overflow
- `SO_EXCLUSIVEADDRUSE` on Windows, `SO_REUSEADDR` on Unix

### FrameV2 schema (v2.1.0+)

Slot frames have these fields: `slot`, `data`, `frame_number`, `severity`, `short_slot`, `extra_slot`, `bitclock_error`, `missed_data`, `missed_frame_sync`, `low_sample_rate`.

## Git Conventions

- Tags: `vX.Y.Z` (e.g. v2.0.0, v2.1.0, v2.2.0) — semantic versioning
- Linear history, no squash, no amend
- Feature branches merged via fast-forward only
- Current feature branch: `feature/live_stream` (audio streaming work)
