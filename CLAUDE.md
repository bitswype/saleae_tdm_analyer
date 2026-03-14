# CLAUDE.md — TDM Analyzer

## Overview

Saleae Logic 2 analyzer plugin for decoding TDM (Time Division Multiplexing) audio data. Three layers:

1. **C++ LLA** (`src/`) — Low Level Analyzer plugin, decodes raw signals into FrameV2 slot frames
2. **Python HLA: WAV Export** (`hla-wav-export/`) — writes selected slots to a WAV file during capture
3. **Python HLA: Audio Stream** (`hla-audio-stream/`) — streams selected slots as live PCM over TCP

Plus two companion Python CLI tools in `tools/`.

## Project Structure

```
src/                          C++ LLA plugin (CMake build)
  TdmAnalyzer.cpp/h           Main analyzer — WorkerThread decodes TDM frames
  TdmAnalyzerSettings.cpp/h   User-configurable settings
  TdmAnalyzerResults.cpp/h    FrameV2 output (10 fields per slot frame)
  TdmSimulationDataGenerator.* Simulation data for Logic 2

hla-wav-export/               Python HLA — WAV export
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

tools/tdm-audio-bridge/       Companion CLI + GUI — plays streamed audio
  _build.py                   Custom setuptools backend (auto-generates _version.py)
  gen_version.py              Generates _version.py from git describe
  tdm_audio_bridge/cli.py     Click CLI: listen, gui, devices
  tdm_audio_bridge/gui.py     tkinter GUI (~400 lines)
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

# Full automated quality sweep across 11 configurations
tdm-test-harness quality-sweep
```

The `analyze` command uses sox to:
- Verify the dominant frequency matches expected
- Detect glitches via notch filter + windowed RMS (50ms windows, -40 dB threshold)
- Detect dropouts via silence removal duration comparison

### Quality sweep test suite (11 tests)

1. Signal integrity: 24kHz, 44.1kHz, 48kHz, 96kHz mono 16-bit
2. Multi-channel: stereo, 4-channel
3. 32-bit depth
4. Loop boundary: phase-perfect 440Hz sine (sox-generated), captures across 2+ boundaries, notch-filter verified — any detected glitch is a pipeline bug, not content mismatch
5. Reconnection resilience: disconnect mid-stream, reconnect, verify clean handshake + data
6. Buffer pressure: 32-frame ring buffer, verify data integrity (frame alignment, amplitude, sign balance) despite heavy overflow

### Sender batching optimization

The HLA's `_sender_loop` batches up to 1024 frames per `sendall()` call. Without batching, stereo streams generate 44100+ individual 4-byte TCP sends per second, starving the decode thread of GIL time.

### WSL2 audio limitations

Native Windows audio playback (via Windows Python + tdm-audio-bridge) is clean. WSL2's WSLg/PulseAudio/RDP audio path produces ALSA underruns that are not pipeline bugs. For audio quality testing, use TCP capture (`tdm-test-harness capture`) which bypasses audio hardware entirely.

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

- Tags: `vX.Y.Z` (e.g. v2.0.0, v2.1.0, v2.2.0, v2.3.0) — semantic versioning
- Linear history, no squash, no amend
- Feature branches merged via fast-forward only
- Remote: SSH (`git@github.com:bitswype/saleae_tdm_analyer.git`)

## CI / Release

- GitHub Actions workflow: `.github/workflows/build.yml`
- Triggers on push to main, tags, and PRs
- Builds C++ LLA for Windows, macOS (x86_64 + arm64), Linux
- Tagged builds create a GitHub Release with `analyzer.zip` containing:
  - Platform-specific LLA binaries
  - `hla-wav-export/` and `hla-audio-stream/` (Python HLAs)
  - `tools/` (tdm-audio-bridge and tdm-test-harness)
  - `README.md` and `LICENSE`
  - Pre-generated `_version.py` (via `gen_version.py` with `fetch-depth: 0`)
- `_build.py` custom setuptools backend auto-generates `_version.py` during `pip install`
