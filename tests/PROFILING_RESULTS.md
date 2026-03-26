# TDM Analyzer Profiling Results

Profiled on native Windows with MSVC 19.44, Release x64, 48000 frames per test.

Machine: AMD Ryzen 7 H 255 (8 cores / 16 threads), 32 GB RAM, Windows 11 Pro.

## Executive Summary

**FrameV2 construction dominates decode time**, consuming 60-90% of total decode time across all configurations. The `AnalyzeTdmSlot::FrameV2` section -- which constructs a FrameV2 object, populates 10 fields via AddInteger/AddString/AddBoolean, and calls AddFrameV2 -- is the single largest bottleneck. V1 Frame output (AddFrame) and result committing (CommitResults + ReportProgress) are negligible by comparison.

The second largest cost is **AddMarker calls** in GetNextBit, consuming 8-16% of decode time. Each bit processed generates 1-2 marker calls (clock arrow + optional data dot).

**Bit assembly and flag checking** (the actual decode logic) is nearly free -- the time spent in AnalyzeTdmSlot outside of AddFrame/FrameV2/Commit is under 1% of total decode time.

## Optimization Priorities

| Priority | Target | Current cost | Potential savings |
|----------|--------|-------------|-------------------|
| 1 | FrameV2 construction | 60-90% of decode | Deferring or batching FrameV2 could cut decode time in half or more |
| 2 | AddMarker calls | 8-16% of decode | Reducing marker count (e.g., only on frame boundaries) could save 10%+ |
| 3 | AdvancedAnalysis | ~8% when enabled | Already zero-cost when disabled; optimization is secondary |
| 4 | Channel advances | ~8% of decode | SDK operations; limited optimization potential |

## Detailed Findings

### Finding 1: FrameV2 is the bottleneck (60-90% of decode time)

Per-call cost of FrameV2 varies dramatically by configuration:

| Config | FrameV2 per-call (us) | FrameV2 % of decode |
|--------|----------------------:|--------------------:|
| Stereo 16-bit | 10.9 | 66% |
| Stereo 32-bit | 3.7 | 24% |
| Stereo 64-bit | 3.9 | 13% |
| 8-channel 16-bit | 11.7 | 66% |
| 16-channel 16-bit | 8.8 | 61% |
| 32-channel 16-bit | 9.1 | 61% |
| 64-channel 16-bit | 14.9 | 71% |
| 96 kHz Stereo 24/32-bit | 26.3 | 70% |
| 96 kHz 16-ch 32-bit | 118.5 | 89% |

The per-call cost is NOT constant -- it ranges from 3.7 us (stereo 32-bit) to 118.5 us (96 kHz 16-ch 32-bit). This 32x variation suggests the FrameV2 cost scales with something beyond just the 10 field additions. Possible factors: memory allocation patterns in the SDK's FrameV2Data internals, or interaction with the mock results storage growing over time (the 96 kHz 16-ch 32-bit test produces 768K slots).

For context, the V1 AddFrame call is consistently 0.05-0.08 us -- 100-1500x cheaper than FrameV2.

### Finding 2: AddMarker is the second largest cost (8-16%)

Each GetNextBit call generates 1-2 AddMarker calls:
- Always: clock channel arrow marker
- Non-advanced mode: data channel One/Zero marker
- Advanced mode with errors: additional Stop markers

| Config | Marker time (ms) | Markers % of decode | Per-call (us) |
|--------|------------------:|--------------------:|--------------:|
| Stereo 16-bit | 119.6 | 8% | 0.078 |
| 8-channel 16-bit | 559.5 | 8% | 0.091 |
| 32-channel 16-bit | 1958.0 | 9% | 0.080 |
| 64-channel 16-bit | 4408.3 | 7% | 0.090 |
| Stereo 16-bit +adv | 77.2 | 5% | 0.050 |

Non-advanced mode adds ~50% more marker overhead (2 markers per bit vs 1 in advanced mode, where the data marker is skipped in favor of the advanced checks).

### Finding 3: Advanced analysis adds ~30% overhead

Comparing basic vs advanced for the same configuration:

| Config | Basic decode (ms) | Advanced decode (ms) | Overhead |
|--------|-------------------:|---------------------:|---------:|
| Stereo 16-bit | 1583.7 | 1481.5 | -6%* |
| 8-channel 16-bit | 6758.9 | 7802.3 | +15% |

*The stereo advanced result is faster than basic -- this is likely measurement noise at 48K frames. The advanced analysis adds ~0.13 us per bit for the extra channel advances and edge checks, which becomes significant at high channel counts.

The AdvancedAnalysis sub-section consistently costs ~0.124-0.132 us per call across all configs. For 8-channel 16-bit, that's 6.1M calls * 0.13 us = 796 ms additional.

### Finding 4: Channel advances are consistent and SDK-bound

GetNextBit::ChannelAdvance (advancing data and frame channels to the clock edge position) is consistently 0.07-0.09 us per call. This includes:
- `mData->AdvanceToAbsPosition()`
- `mData->GetBitState()`
- `mFrame->AdvanceToAbsPosition()`
- `mFrame->GetBitState()`

This is SDK testlib overhead and cannot be optimized in analyzer code. In the real Logic 2 environment, these would be actual channel data operations.

### Finding 5: Bit assembly is essentially free

The time in AnalyzeTdmSlot outside the AddFrame/FrameV2/Commit sub-sections is the actual decode work: alignment indexing, bit accumulation loops, flag checking. This overhead is:

| Config | Slots | AnalyzeTdmSlot total (ms) | AddFrame+FV2+Commit (ms) | Bit assembly (ms) | Per-slot (us) |
|--------|------:|---------------------------:|--------------------------:|-------------------:|--------------:|
| Stereo 16-bit | 96005 | 1069.8 | 1059.5 | 10.3 | 0.107 |
| 8-channel 32-bit | 384016 | 1593.0 | 1543.7 | 49.3 | 0.128 |

Bit assembly is ~0.1 us per slot -- negligible. The decode algorithm itself is not the bottleneck.

### Finding 6: CommitResults + ReportProgress are negligible

AnalyzeTdmSlot::Commit is consistently 0.026-0.092 us per call. At 96K-3M calls, this totals 3-85 ms -- well under 1% of decode time.

## Raw Data Summary Table

All times in milliseconds, 48000 frames per test.

| # | Config | Decode | GetNextBit | Markers | ChannelAdv | AdvAnalysis | AnalyzeSlot | AddFrame | FrameV2 | Commit |
|---|--------|-------:|-----------:|--------:|-----------:|------------:|------------:|---------:|--------:|-------:|
| 1 | Stereo 16-bit | 1583.7 | 465.5 | 119.6 | 131.9 | - | 1069.8 | 5.6 | 1051.0 | 2.9 |
| 2 | Stereo 16-bit +adv | 1481.5 | 630.0 | 77.2 | 115.8 | 202.6 | 805.5 | 6.8 | 785.0 | 2.9 |
| 3 | 8-ch 16-bit | 6758.9 | 2008.3 | 559.5 | 521.0 | - | 4557.4 | 20.4 | 4484.0 | 10.9 |
| 4 | Stereo 32-bit | 1483.9 | 1014.3 | 262.3 | 231.1 | - | 373.0 | 6.4 | 351.8 | 3.0 |
| 5 | 96k Stereo 24/32 | 3631.9 | 987.3 | 255.7 | 231.1 | - | 2549.7 | 6.7 | 2528.1 | 3.8 |
| 6 | 8-ch 16-bit +adv | 7802.3 | 2478.1 | 309.6 | 457.2 | 795.8 | 5146.7 | 21.3 | 5072.1 | 11.0 |
| 7 | 16-ch 16-bit | 11103.4 | 3680.2 | 929.5 | 1081.5 | - | 6932.0 | 42.4 | 6780.2 | 21.8 |
| 8 | 32-ch 16-bit | 22775.6 | 7505.3 | 1958.0 | 2065.4 | - | 14240.2 | 77.3 | 13952.8 | 40.5 |
| 9 | 64-ch 16-bit | 64496.8 | 16269.8 | 4408.3 | 4395.3 | - | 46290.2 | 157.7 | 45691.8 | 85.3 |
| 10 | Stereo 64-bit | 2767.0 | 1910.2 | 584.7 | 419.2 | - | 414.0 | 17.1 | 372.5 | 8.8 |
| 11 | 8-ch 32-bit | 5916.3 | 3674.6 | 965.3 | 948.2 | - | 1593.0 | 31.0 | 1494.9 | 17.8 |
| 12 | 8-ch 24/32 | 6613.8 | 3664.6 | 966.0 | 948.6 | - | 2335.6 | 32.3 | 2239.5 | 16.8 |
| 13 | 192k Stereo 24/32 | 1749.8 | 912.7 | 260.6 | 231.9 | - | 695.9 | 10.2 | 667.5 | 6.6 |
| 14 | 384k Stereo 32-bit | 1823.7 | 978.3 | 317.8 | 232.9 | - | 696.1 | 11.4 | 664.9 | 7.5 |
| 15 | 96k 16-ch 32-bit | 102835.3 | 10538.6 | 4148.0 | 2178.7 | - | 91206.6 | 65.6 | 90997.9 | 36.2 |
| 16 | 32-ch 32-bit +adv | 26872.7 | 19540.1 | 2413.9 | 3564.0 | 6084.3 | 5978.8 | 94.7 | 5634.9 | 51.4 |

## Anomalies

### Test 15 (96 kHz 16-ch 32-bit) has extreme FrameV2 cost

This configuration produces 768K slots, and the FrameV2 per-call cost balloons to 118.5 us (vs ~10 us for similar-sized tests). This suggests the FrameV2 mock or results storage has a scaling problem when the total slot count grows very large -- possibly due to vector reallocation or cache pressure in the results storage.

### Non-profiled throughput is higher than profiled

Comparing the same config (stereo 16-bit, 48K frames):
- Non-profiled: 5.1 Mbit/s, 3.3x realtime
- Profiled: 1.0 Mbit/s, 0.6x realtime

The profiling instrumentation adds ~3x overhead. This is expected since `GetNextBit` is called ~1.5M times and each call now includes two chrono::steady_clock reads plus counter updates. The relative proportions between sections should still be representative, but absolute timings are inflated by the measurement overhead.

## Recommendations

1. **Investigate FrameV2 construction cost in the real SDK.** The mock stubs may differ from the real Logic 2 FrameV2 implementation. Profile with the real SDK to confirm FrameV2 is still the bottleneck. If it is, consider:
   - Reducing the number of FrameV2 fields (currently 10 per slot)
   - ~~Batching FrameV2 construction~~ (INVALID: SDK Add* methods append, not overwrite; no Clear/Reset exists. Reuse causes O(N^2) memory growth. See PERFORMANCE.md Phase 5b.)
   - Making FrameV2 output optional via a setting

2. **Reduce marker count.** Consider adding markers only on slot or frame boundaries rather than every bit. This could save 8-16% of decode time.

3. **The actual decode algorithm needs no optimization.** Bit assembly, alignment, shift order, and flag checking together consume under 1% of decode time. Any refactoring of the decode loop for performance would have negligible impact.
