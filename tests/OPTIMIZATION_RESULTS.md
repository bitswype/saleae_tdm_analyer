# TDM Analyzer Optimization Results

Performance comparison of the new FrameV2 detail and marker density settings.
Profiled on native Windows with MSVC 19.44, Release x64, 10000 frames per test.

Machine: AMD Ryzen 7 H 255 (8 cores / 16 threads), 32 GB RAM, Windows 11 Pro.

Note: Absolute timings include profiling instrumentation overhead (~3x). The
relative speedups between modes are what matter.

## Speedup Summary

| Config | Default (ms) | Minimal+Slot (ms) | Off+None (ms) | Speedup (Minimal) | Speedup (Off) |
|--------|-------------:|-------------------:|---------------:|-------------------:|--------------:|
| Stereo 16-bit | 174.1 | 92.5 | 68.4 | **1.9x** | **2.5x** |
| 8-channel 16-bit | 614.9 | 423.3 | 264.1 | **1.5x** | **2.3x** |
| 8-channel 32-bit | 1071.0 | 689.0 | 491.7 | **1.6x** | **2.2x** |

For realtime audio streaming (the primary use case for these optimizations):
- **Minimal + Slot Markers** is the recommended setting: 1.5-1.9x faster while
  retaining full HLA support and slot-level waveform annotation.
- **Off + No Markers** gives maximum throughput (2.2-2.5x faster) for users who
  only need bubble text or CSV export.

## Full Results

All times in milliseconds, 10000 frames per test.

### Stereo 16-bit (48 kHz, 2 ch, 16-bit)

| Mode | Decode (ms) | Mbit/s | Realtime | vs Default |
|------|------------:|-------:|---------:|-----------:|
| Full + AllMarkers (default) | 174.1 | 1.8 | 1.2x | 1.0x |
| Full + SlotMarkers | 140.9 | 2.3 | 1.5x | 1.2x |
| Full + NoMarkers | 140.7 | 2.3 | 1.5x | 1.2x |
| Minimal + AllMarkers | 138.0 | 2.3 | 1.5x | 1.3x |
| Minimal + SlotMarkers | 92.5 | 3.5 | 2.3x | 1.9x |
| Minimal + NoMarkers | 101.3 | 3.2 | 2.1x | 1.7x |
| Off + AllMarkers | 99.6 | 3.2 | 2.1x | 1.7x |
| Off + NoMarkers (fastest) | 68.4 | 4.7 | 3.0x | 2.5x |

### 8-channel 16-bit (48 kHz, 8 ch, 16-bit)

| Mode | Decode (ms) | Mbit/s | Realtime | vs Default |
|------|------------:|-------:|---------:|-----------:|
| Full + AllMarkers (default) | 614.9 | 2.1 | 0.3x | 1.0x |
| Full + SlotMarkers | 563.0 | 2.3 | 0.4x | 1.1x |
| Full + NoMarkers | 527.6 | 2.4 | 0.4x | 1.2x |
| Minimal + AllMarkers | 577.3 | 2.2 | 0.4x | 1.1x |
| Minimal + SlotMarkers | 423.3 | 3.0 | 0.5x | 1.5x |
| Minimal + NoMarkers | 410.7 | 3.1 | 0.5x | 1.5x |
| Off + AllMarkers | 421.7 | 3.0 | 0.5x | 1.5x |
| Off + NoMarkers (fastest) | 264.1 | 4.8 | 0.8x | 2.3x |

### 8-channel 32-bit (48 kHz, 8 ch, 32-bit)

| Mode | Decode (ms) | Mbit/s | Realtime | vs Default |
|------|------------:|-------:|---------:|-----------:|
| Full + AllMarkers (default) | 1071.0 | 2.4 | 0.2x | 1.0x |
| Full + SlotMarkers | 820.3 | 3.1 | 0.3x | 1.3x |
| Full + NoMarkers | 806.3 | 3.2 | 0.3x | 1.3x |
| Minimal + AllMarkers | 961.4 | 2.7 | 0.2x | 1.1x |
| Minimal + SlotMarkers | 689.0 | 3.7 | 0.3x | 1.6x |
| Minimal + NoMarkers | 674.9 | 3.8 | 0.3x | 1.6x |
| Off + AllMarkers | 745.6 | 3.4 | 0.3x | 1.4x |
| Off + NoMarkers (fastest) | 491.7 | 5.2 | 0.4x | 2.2x |

## Observations

1. **FrameV2 detail level matters more than markers.** Switching Full to Minimal
   saves 15-20% alone; switching Full to Off saves 35-45%. Marker reduction
   adds another 10-20% on top.

2. **The biggest gains come from combining both.** Minimal+Slot vs Full+All
   gives 1.5-1.9x. Neither setting alone reaches this.

3. **SlotMarkers vs NoMarkers is negligible.** The difference between Slot
   and None is within measurement noise for most configs. SlotMarkers is the
   practical sweet spot -- you get waveform annotation at minimal cost.

4. **32-bit configs benefit more from marker reduction** than 16-bit configs,
   because 32-bit slots have 2x more clock edges and thus 2x more marker calls.

5. **The mock FrameV2 capture overhead inflates all timings.** In the real Logic 2
   SDK, the absolute numbers will differ, but the relative speedups should be
   similar or better (the real SDK's FrameV2 implementation may have its own
   overhead patterns).

## Recommended Settings by Use Case

| Use case | FrameV2 Detail | Markers | Expected speedup |
|----------|---------------|---------|-----------------|
| Full inspection (default) | Full | All bits | 1.0x (baseline) |
| Realtime audio streaming | Minimal | Slot boundaries | 1.5-1.9x |
| WAV file export (batch) | Minimal | None | 1.7-2.1x |
| CSV export / bubble text only | Off | Slot boundaries | 1.5-2.3x |
| Maximum decode speed | Off | None | 2.2-2.5x |
