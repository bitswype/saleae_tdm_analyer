# Benchmark Baseline v2.5.0

Machine: AMD Ryzen 7 H 255, Windows 11 Pro, MSVC 19.44
Date: 2026-04-06
Frames per test: 48000 (1 second at 48 kHz)

## Target configuration: 8ch/32bit/48kHz

| Mode | Decode (ms) | Mbit/s | RT multiplier |
|------|----------:|-------:|:-------------:|
| Full + AllMarkers (default) | 3649 | 3.4 | 0.3x |
| Full + SlotMarkers | 3035 | 4.0 | 0.3x |
| Minimal + SlotMarkers | 2033 | 6.0 | 0.5x |
| Off + NoMarkers | 1617 | 7.6 | 0.6x |
| Batch=64 + SlotMarkers | 2513 | 4.9 | 0.4x |
| Batch=256 + NoMarkers | 2427 | 5.1 | 0.4x |

**Goal: 1.0x real-time = 1000ms decode for 1s of 8ch/32bit/48kHz**
**Current best: 1617ms (Off+None) = needs 1.6x speedup in GetNextBit()**

## Throughput ceiling

~7.5 Mbit/s regardless of configuration. This is the GetNextBit() loop
throughput - the fundamental per-bit SDK call overhead.

## Full comparison matrix

See benchmark output for all 16 base configs, optimized modes,
performance mode comparison (4 configs x 8 modes), and batch mode
comparison (4 configs x 3 batch sizes).
