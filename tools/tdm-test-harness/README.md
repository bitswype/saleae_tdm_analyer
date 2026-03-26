# TDM Test Harness

Standalone testing tool for the TDM Audio Stream HLA. Drives the HLA outside
Logic 2 with synthetic signals, verifies decode accuracy, and runs automated
quality sweeps. No Saleae hardware or Logic 2 installation needed.

## Install

```bash
pip install tools/tdm-test-harness/   # from repo root
# or
pip install tdm-test-harness/         # from tools/ directory
```

## Commands

### verify - Automated pass/fail test

Generates a test signal, runs it through the HLA, captures the TCP output,
and verifies the decoded audio matches the expected frequency.

```bash
# Basic stereo sine test
tdm-test-harness verify --signal sine:440 --duration 0.5 --json

# Multi-channel
tdm-test-harness verify --signal sine:440,880,1320,1760 --channels 4 --json

# 32-bit depth
tdm-test-harness verify --signal ramp --bit-depth 32 --json
```

Exit code 0 = pass, 1 = fail. `--json` outputs structured results for CI.

### serve - Run the HLA as a TCP server

```bash
tdm-test-harness serve --signal sine:440 --port 4011
```

Useful for manual testing with tdm-audio-bridge or any TCP client.

### capture - Record a TCP stream to WAV

```bash
tdm-test-harness capture --port 4011 --duration 3 --skip 0.5 -o test.wav
```

### analyze - Audio quality analysis (requires sox)

```bash
tdm-test-harness analyze test.wav --freq 440
```

Checks for frequency accuracy, glitches (via notch filter + windowed RMS),
and dropouts (via silence detection).

### quality-sweep - Full automated test suite

```bash
tdm-test-harness quality-sweep
```

Runs 11 test configurations covering:
1. Sample rates: 24 kHz, 44.1 kHz, 48 kHz, 96 kHz
2. Multi-channel: stereo, 4-channel
3. 32-bit depth
4. Loop boundary: phase-perfect sine verified with notch filter
5. Reconnection resilience
6. Buffer pressure: 32-frame ring buffer overflow

### signals - List available signal types

```bash
tdm-test-harness signals
```

### profile - HLA performance profiling

```bash
tdm-test-harness profile --channels 8 --duration 1.0
```

Drives the HLA with synthetic data and reports per-section timing breakdown.
Requires the `TDM_HLA_PROFILE=1` environment variable for detailed counters.

## Architecture

```
tdm_test_harness/
  cli.py            Click CLI entry point (all commands above)
  signals.py        Signal generators (sine, silence, ramp, WAV file)
  frame_emitter.py  Converts samples to fake AnalyzerFrame objects
  hla_driver.py     Drives the HLA outside Logic 2
  verifier.py       TCP client for automated verification
```
