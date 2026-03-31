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

### C++ tests (correctness + benchmark)

```bash
# Build (Linux/macOS)
cmake -B build-test -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-test --target tdm_correctness --target tdm_benchmark

# Build (Windows — from Developer Command Prompt)
cmake -B build-test -DBUILD_TESTS=ON -A x64
cmake --build build-test --config Release --target tdm_correctness --target tdm_benchmark

# Correctness tests (exit code 0 = pass)
./build-test/tests/tdm_correctness              # Linux/macOS
build-test\bin\Release\tdm_correctness.exe       # Windows

# Performance benchmark (48000 frames = 1 second at 48 kHz)
./build-test/tests/tdm_benchmark 48000           # Linux/macOS
build-test\bin\Release\tdm_benchmark.exe 48000   # Windows

# Profiled benchmark (per-function timing breakdown)
cmake -B build-prof -DBUILD_TESTS=ON -DENABLE_PROFILING=ON -A x64     # Windows
cmake -B build-prof -DBUILD_TESTS=ON -DENABLE_PROFILING=ON -DCMAKE_BUILD_TYPE=Release  # Linux/macOS
cmake --build build-prof --config Release --target tdm_benchmark
build-prof\bin\Release\tdm_benchmark.exe 1000    # Windows
```

Profiling uses compile-time instrumentation (`src/TdmProfiler.h`) that expands to nothing when `ENABLE_PROFILING` is OFF. When ON, the benchmark prints a per-function timing breakdown after each configuration showing where decode time is spent (GetNextBit, ChannelAdvance, Markers, FrameV2 construction, etc.).

**Real SDK benchmark timing** (for measuring decode performance inside Logic 2):

```bash
# Build with self-timing enabled
cmake -B build-bench -DENABLE_BENCHMARK_TIMING=ON -A x64              # Windows
cmake -B build-bench -DENABLE_BENCHMARK_TIMING=ON -DCMAKE_BUILD_TYPE=Release  # Linux/macOS
cmake --build build-bench --config Release
```

When `ENABLE_BENCHMARK_TIMING` is ON, the DLL records `steady_clock` timestamps at decode start and after each TDM frame. On analyzer destruction (closing the capture tab), it writes timing to `%USERPROFILE%\tdm_benchmark_timing.json`. Use with `tools/prepare_benchmark_captures.py` to generate .sal files with `showInDataTable=false` and `streamToTerminal=false` for accurate measurement without UI overhead. See `tests/PERFORMANCE.md` Phase 9 for the full methodology.

**Correctness tests** (`tdm_correctness`): 67 tests in eleven categories: happy path (11), sign conversion (6), error conditions (3), combination tests (6), robustness/misconfig (6), bit pattern coverage (1), boundary values (10), advanced analysis error detection (3), generator blind spot tests (4), FrameV2 field verification (8), and audio batch mode (9, verifying batch emission, PCM correctness, frame numbering, sample rate propagation, V1 frame preservation, 32-bit packing, signed values, and batch=1).

**Benchmark** (`tdm_benchmark`): 16 throughput configurations (stereo through 64-channel, 16-bit through 64-bit, with/without advanced analysis). See `tests/BENCHMARK_BASELINE.md` for baseline numbers.

### Performance tuning settings

Three user-facing settings control the speed/detail tradeoff:

- **Data Table / HLA Output** (`mFrameV2Detail`): Full (all 10 FrameV2 fields), Minimal (5 fields needed by audio HLAs), or Off (no FrameV2, maximum speed). Default: Full.
- **Waveform Markers** (`mMarkerDensity`): All bits (per-bit arrows + data dots), Slot boundaries only, or None. Default: All bits.
- **Audio Batch Size** (`mAudioBatchSize`): Off (one FrameV2 per slot, default) or 1-1024 (powers of 2) TDM frames per FrameV2. When enabled, the Data Table / HLA Output setting is ignored and only `audio_batch` FrameV2 frames with packed PCM data are emitted. Required for real-time streaming at stereo 48kHz or above. V1 Frames, markers, and bubble text are unaffected.

Profiling showed FrameV2 construction at 60-90% of decode time and markers at 8-16%. For realtime audio streaming, set Minimal + Slot boundaries (1.8x speedup validated on real SDK). See `tests/PERFORMANCE.md` for the full story from baseline through profiling to optimization.

**CRITICAL: Logic 2 UI display settings.** Disabling "Show in data table" and "Stream to terminal" (right-click analyzer in sidebar) is **mandatory for realtime streaming**. With these enabled, indexing overhead is 50-100x the actual decode time. This dwarfs any analyzer-side optimization. Real SDK measurements (Phase 9 in PERFORMANCE.md):
- Full+All: 2.83s decode, ~126s with UI display ON
- Minimal+Slot: 1.54s decode, ~103s with UI display ON
- Off+None: 0.91s decode, ~1.2s with UI display ON (nothing to index)

### Real-time streaming throughput ceiling

**The HLA progress indicator shows percentage of real-time throughput.** It must show **100%** for real-time audio streaming to work. Any value below 100% means the audio bridge will underrun.

Logic 2's Python HLA pipeline has a hard ceiling of approximately **50,000 decode() calls per second**. This is dispatch overhead in Logic 2's framework, not our decode logic. Cython makes no measurable difference to this ceiling.

| Configuration | decode() calls/sec | HLA throughput | Real-time? |
|---------------|-------------------:|---------------:|:----------:|
| Stereo 24kHz (no batch)  | 48,000   | 100%           | Yes        |
| Stereo 48kHz (no batch)  | 96,000   | ~54%           | No         |
| Stereo 48kHz (batch=2)   | 24,000   | 100%           | Yes        |
| Stereo 48kHz (batch=64)  | 750      | 100%           | Yes        |

**Audio Batch Mode is the solution.** Setting Audio Batch Size to any value >= 2 brings stereo 48kHz well under the 50k ceiling. Batch=64 is recommended for comfortable headroom. Without batching, mono does not help either - the LLA emits one FrameV2 per slot regardless of the HLA's slot filter. See `tests/PERFORMANCE.md` Phase 10.

### HLA Cython fast decode

The audio stream HLA has a Cython-compiled fast decode path (`_decode_fast.pyx`) that moves the entire decode() body to C: field extraction, slot filtering, frame boundary detection, sign conversion, sample accumulation, and PCM packing. Falls back gracefully to pure Python when the extension is not compiled.

**Important:** Logic 2's embedded Python does not include the HLA directory in `sys.path`. The HLA adds its own directory to `sys.path` at module load time so compiled extensions (`.pyd`/`.so`) can be found. Without this, the Cython backend silently fails to load.

```bash
# Build the Cython extension
cd hla-audio-stream
pip install cython
python setup_cython.py build_ext --inplace
```

Four backends were implemented and compared (Cython, raw C extension, cffi, pure Python). Cython is the fastest (4-7x over baseline), with a four-tier fallback chain: Cython > rawc > cffi > Python. See `tests/PERFORMANCE.md` for the full comparison and `tests/C_EXTENSION_DESIGN.md` for the design rationale.

All backends validated by a 64-test oracle (`tests/test_hla_decode.py`) covering every branch of decode() including C-port-specific edge cases.

### Sender batching optimization

The HLA's `_sender_loop` batches up to 1024 frames per `sendall()` call. Without batching, stereo streams generate 44100+ individual 4-byte TCP sends per second, starving the decode thread of GIL time.

### WSL2 audio limitations

Native Windows audio playback (via Windows Python + tdm-audio-bridge) is clean. WSL2's WSLg/PulseAudio/RDP audio path produces ALSA underruns that are not pipeline bugs. For audio quality testing, use TCP capture (`tdm-test-harness capture`) which bypasses audio hardware entirely.

## Critical Patterns

### FrameV2 SDK behavior (IMPORTANT)

- **FrameV2 Add* methods APPEND, they do not overwrite.** `AddInteger("slot", 1)` followed by `AddInteger("slot", 2)` results in TWO entries, not one.
- **FrameV2 has no Clear() or Reset() method.** There is no way to remove entries from a FrameV2 object.
- **NEVER reuse a FrameV2 across frames.** Always create a fresh local `FrameV2` per frame. Reusing a member variable causes O(N^2) memory growth and OOM crashes. This was tried and reverted after crashing Logic 2 with 28 GB memory consumption on a <1 second capture. See `tests/PERFORMANCE.md` Phase 5b for the full post-mortem.
- **The test mock diverges here.** The mock uses `std::map` (overwrites by key). The real SDK uses an append-only list. This divergence hid the bug during mock-based benchmarking.

### Logic 2 HLA conventions

- **Logic 2 instantiates HLAs multiple times** — a setup pass then a capture pass. The first instance is never cleaned up (no `shutdown()` call). TCP servers must use class-level `_prev_instance` tracking + `gc.get_objects()` socket scan to close stale sockets before binding.
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

- Tags: `vX.Y.Z` (e.g. v2.0.0, v2.1.0, v2.2.0, v2.3.0) - semantic versioning
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
