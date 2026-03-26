# Tests and Performance Documentation

This directory contains the test suites, benchmarks, and performance analysis
for the TDM Analyzer. If you're new here, start with **Running the tests**
below, then browse the docs that interest you.

## Running the tests

### C++ correctness tests (58 tests, no Logic 2 needed)

```bash
# Build
cmake -B build-test -DBUILD_TESTS=ON -A x64                          # Windows
cmake -B build-test -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release      # Linux/macOS
cmake --build build-test --config Release --target tdm_correctness

# Run (exit code 0 = pass)
build-test\bin\Release\tdm_correctness.exe       # Windows
./build-test/tests/tdm_correctness               # Linux/macOS
```

### Python HLA tests (64 tests, no Logic 2 needed)

```bash
pip install pytest
pytest tests/test_hla_decode.py -v
```

### C++ performance benchmark

```bash
cmake --build build-test --config Release --target tdm_benchmark
build-test\bin\Release\tdm_benchmark.exe 48000   # Windows (48000 frames = 1s at 48 kHz)
```

### Real SDK benchmark (requires Logic 2 + hardware)

```bash
# Build with self-timing enabled
cmake -B build-bench -DENABLE_BENCHMARK_TIMING=ON -A x64
cmake --build build-bench --config Release

# Deploy the DLL to Logic 2's custom analyzer directory, then:
# 1. Generate benchmark .sal files with UI display off:
python tools/prepare_benchmark_captures.py "path/to/source.sal" tmp/benchmark_captures

# 2. Open each .sal in Logic 2, wait for processing, close the tab
# 3. Read the timing:
cat %USERPROFILE%\tdm_benchmark_timing.json
```

See [PERFORMANCE.md](PERFORMANCE.md) Phase 9 for the full methodology.

## Documentation guide

| Document | What's in it | Who should read it |
|----------|-------------|-------------------|
| [TESTING.md](TESTING.md) | Test architecture, categories, audit history, known gaps | Contributors adding tests or understanding coverage |
| [PERFORMANCE.md](PERFORMANCE.md) | The full performance story from Phase 1 (safety net) through Phase 9 (real SDK validation), including 18 lessons learned | Anyone interested in performance, optimization methodology, or SDK behavior |
| [BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md) | Raw throughput numbers: MSVC vs GCC, 16 configurations, WSL2 vs native Windows | Comparing platforms or reproducing baseline measurements |
| [PROFILING_RESULTS.md](PROFILING_RESULTS.md) | Per-function timing breakdown across 16 configurations with 9 instrumented sections | Understanding where decode time is spent |
| [OPTIMIZATION_RESULTS.md](OPTIMIZATION_RESULTS.md) | LLA mode comparison: Full/Minimal/Off x All/Slot/None marker settings | Choosing the right settings for your use case |
| [C_EXTENSION_DESIGN.md](C_EXTENSION_DESIGN.md) | HLA C extension design: why Cython beat raw C and cffi, scope levels, fallback strategy | Anyone considering C extensions for Saleae HLAs |

## Key findings (the short version)

- **Disabling "Show in data table" and "Stream to terminal" in Logic 2 is 50-100x more impactful than any code optimization.** This is the single most important thing for realtime streaming users.
- **Minimal+Slot is the recommended setting for audio streaming** - 1.8x faster than the default, provides all fields the HLAs need.
- **The Cython HLA backend is 4-7x faster than pure Python** - but the LLA is the pipeline bottleneck, not the HLA.
- **FrameV2 SDK behavior: Add\* methods append, never overwrite.** There is no Clear/Reset. Always use a fresh local FrameV2 per frame. See PERFORMANCE.md Phase 5b for the post-mortem on what happens when you don't.

## Test file overview

| File | Tests | What it covers |
|------|------:|---------------|
| `test_decode_values.cpp` | 27 | Happy path decoding, combinations, boundary values |
| `test_sign_conversion.cpp` | 9 | Signed/unsigned conversion (unit + end-to-end + FrameV2) |
| `test_error_conditions.cpp` | 9 | Error flags, misconfig robustness |
| `test_advanced_analysis.cpp` | 3 | Hand-crafted bitclock/missed-data/missed-frame-sync errors |
| `test_generator_blindspots.cpp` | 3 | Padding HIGH bits, DSP Mode A offset, low sample rate |
| `test_framev2.cpp` | 7 | FrameV2 field correctness (severity, booleans, frame numbering) |
| `test_hla_decode.py` | 64 | Full HLA decode path for all 4 backends (Cython/rawc/cffi/Python) |
| `tdm_benchmark.cpp` | - | 16-config throughput benchmark with optional profiling |
| `tdm_correctness.cpp` | - | Test runner (no tests of its own, runs all .cpp test files) |
