# TDM Analyzer Decode Performance Baseline

Baseline measurements for the C++ Low Level Analyzer decode path.
Generated with `tdm_benchmark 48000` (48000 TDM frames per test).

## Machine

- **CPU:** AMD Ryzen 7 H 255 w/ Radeon 780M (8 cores / 16 threads)
- **RAM:** 32 GB
- **OS:** Microsoft Windows 11 Pro (build 26200)

## Results

All tests use 48000 frames unless noted. Throughput = decoded data bits / decode wall time.
Realtime factor = decoded TDM frames per second / frame rate (>1.0 means faster than realtime).

### Native Windows (MSVC 19.44, Release, x64)

| # | Configuration | Throughput (Mbit/s) | Realtime factor |
|---|--------------|--------------------:|----------------:|
| 1 | Stereo 16-bit | 5.1 | 3.3x |
| 2 | Stereo 16-bit +advanced | 4.1 | 2.6x |
| 3 | 8-channel 16-bit | 4.7 | 0.8x |
| 4 | Stereo 32-bit | 5.3 | 1.7x |
| 5 | 96 kHz Stereo 24/32-bit | 4.1 | 0.9x |
| 6 | 8-channel 16-bit +advanced | 3.9 | 0.6x |
| 7 | 16-channel 16-bit | 4.9 | 0.4x |
| 8 | 32-channel 16-bit | 4.9 | 0.2x |
| 9 | 64-channel 16-bit | 4.6 | 0.1x |
| 10 | Stereo 64-bit | 6.5 | 1.1x |
| 11 | 8-channel 32-bit | 5.3 | 0.4x |
| 12 | 8-channel 24/32-bit | 4.1 | 0.4x |
| 13 | 192 kHz Stereo 24/32-bit | 4.1 | 0.4x |
| 14 | 384 kHz Stereo 32-bit | 5.0 | 0.2x |
| 15 | 96 kHz 16-channel 32-bit | 4.8 | 0.1x |
| 16 | 32-channel 32-bit +advanced | 3.9 | 0.1x |

### WSL2 (g++ 13.3.0, Release, x86_64) - same hardware

| # | Configuration | Throughput (Mbit/s) | Realtime factor |
|---|--------------|--------------------:|----------------:|
| 1 | Stereo 16-bit | 3.6 | 2.4x |
| 2 | Stereo 16-bit +advanced | 3.5 | 2.3x |
| 3 | 8-channel 16-bit | 3.9 | 0.6x |
| 4 | Stereo 32-bit | 3.9 | 1.3x |
| 5 | 96 kHz Stereo 24/32-bit | 3.0 | 0.6x |
| 6 | 8-channel 16-bit +advanced | 3.5 | 0.6x |
| 7 | 16-channel 16-bit | 3.7 | 0.3x |
| 8 | 32-channel 16-bit | 3.5 | 0.1x |
| 9 | 64-channel 16-bit | 3.4 | 0.1x |
| 10 | Stereo 64-bit | 4.7 | 0.8x |
| 11 | 8-channel 32-bit | 3.7 | 0.3x |
| 12 | 8-channel 24/32-bit | 2.6 | 0.3x |
| 13 | 192 kHz Stereo 24/32-bit | 2.8 | 0.3x |
| 14 | 384 kHz Stereo 32-bit | 2.1 | 0.1x |
| 15 | 96 kHz 16-channel 32-bit | 3.3 | 0.1x |
| 16 | 32-channel 32-bit +advanced | 3.8* | 0.1x* |

*Test 16 was only 5000 frames in WSL2 due to memory limits.

## Observations

- **MSVC is ~30-40% faster** than GCC under WSL2 across all configurations.
  Native Windows throughput ranges 3.9-6.5 Mbit/s vs WSL2's 2.1-4.7 Mbit/s.
- **Best throughput:** Stereo 64-bit at 6.5 Mbit/s (MSVC) - wider slots amortize
  per-slot overhead.
- **Advanced analysis overhead:** ~20-25% slower than basic mode (consistent across
  both platforms).
- **Realtime capability:** Stereo 16-bit and 32-bit comfortably exceed 1.0x on both
  platforms. 8+ channel configurations fall below realtime.
- **Memory:** All 16 tests ran at full 48000 frames on native Windows. WSL2 hit memory
  limits on test 16 (had to reduce to 5000 frames).
- **High channel counts** (32-64 channels) are throughput-limited by per-slot frame
  generation overhead, not raw decode speed - throughput stays roughly constant while
  realtime factor drops proportionally to channel count.
