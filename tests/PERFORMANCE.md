# TDM Analyzer Performance Story

This document captures the full journey from "no tests" to measured optimizations,
including what we learned, what surprised us, and what the data means for users.

## The Starting Point

The TDM analyzer had a benchmark (`tdm_benchmark`) that measured overall decode
throughput across 16 configurations, but no correctness tests and no visibility
into where time was being spent. We knew the throughput numbers (3-5 Mbit/s on
MSVC, 2-4 Mbit/s on GCC/WSL2) but not whether the code was correct or where
optimization effort should go.

## Phase 1: Build a Safety Net

Before touching any performance-sensitive code, we needed confidence that changes
wouldn't break decode correctness. The test suite was built over three rounds,
each driven by independent adversarial audit agents:

**Round 1 (20 tests):** Happy path value correctness, sign conversion unit tests,
and basic error conditions. Verified decoded values match expected counting
patterns across all TDM setting combinations.

**Round 2 (50 tests):** Three audit agents identified gaps: counter values never
exercised high bits, no non-power-of-2 widths, advanced analysis errors never
triggered, misconfig tests too weak, signed path never tested end-to-end. Also
found and fixed a 64-bit shift overflow bug (`1ULL << 64` is undefined behavior)
in the signal generator.

**Round 3 (58 tests):** Three more agents found the dominant blind spot: the
entire FrameV2 layer was untestable because stubs were no-ops. Any mutation to
signed conversion, severity strings, error booleans, or frame numbering would
be invisible. We built a FrameV2 capture mock that records field values during
test runs, then added 8 verification tests. This was the single highest-impact
improvement to the test suite.

**Round 4 (cleanup):** Three final agents audited the split and documentation.
Fixed: cross-file coupling, dead code, fragile signal construction, category
mismatches, and missing documentation. Tests split into 7 focused files.

**Key lesson:** Building the test suite before optimizing paid for itself
immediately. The FrameV2 capture mock we created for testing later became
essential for measuring optimization impact. And the correctness tests caught
real issues (the 64-bit UB bug) that had been silently wrong.

See [TESTING.md](TESTING.md) for the full test architecture and audit history.

## Phase 2: Instrument to Understand

With 58 tests as a safety net, we added compile-time profiling instrumentation
(`src/TdmProfiler.h`). The profiling macros expand to nothing when
`ENABLE_PROFILING` is off -- zero overhead in normal builds. When enabled, scoped
timers accumulate per-section call counts and elapsed time.

We instrumented 9 sections of the decode pipeline:

| Section | What it measures |
|---------|-----------------|
| GetTdmFrame | Overall per-frame orchestration |
| GetNextBit | Per-bit hot loop (called millions of times) |
| GetNextBit::ChannelAdvance | SDK channel data operations |
| GetNextBit::Markers | AddMarker calls |
| GetNextBit::AdvancedAnalysis | Extra error detection checks |
| AnalyzeTdmSlot | Per-slot processing |
| AnalyzeTdmSlot::AddFrame | V1 Frame output |
| AnalyzeTdmSlot::FrameV2 | FrameV2 construction + output |
| AnalyzeTdmSlot::Commit | CommitResults + ReportProgress |

## Phase 3: The Profiling Surprise

The profiling results were not what we expected.

**What we assumed:** The decode algorithm (bit assembly, shift order, alignment
indexing) would be the bottleneck, with SDK channel operations close behind.

**What the data showed:**

| Component | % of decode time | Per-call cost |
|-----------|----------------:|-------------:|
| **FrameV2 construction** | **60-90%** | 3.7-118.5 us |
| AddMarker | 8-16% | 0.078-0.091 us |
| Channel advances (SDK) | ~8% | 0.07-0.09 us |
| Bit assembly + decode | **<1%** | 0.1 us |
| V1 AddFrame | <0.5% | 0.05-0.08 us |
| CommitResults | <0.1% | 0.03 us |

**The actual decode algorithm -- the thing we were writing tests for -- was
essentially free.** All the cost was in producing output, not in computing it.

The FrameV2 cost was particularly striking:
- 10 field additions per slot (3 integers, 1 string, 6 booleans)
- Each involves a std::map insertion with string key allocation
- Constructor/destructor allocates and frees a FrameV2Data heap object per slot
- V1 AddFrame does the same work for 0.05-0.08 us -- **100-1500x cheaper**

The 32x variation in FrameV2 per-call cost (3.7 us for stereo 32-bit vs 118.5 us
for 96 kHz 16-ch 32-bit) turned out to be a test mock artifact: the capture
vector growing to 768K entries caused cache thrashing and reallocation spikes.
This is mock-specific and the real SDK will have different scaling characteristics.

**Key lesson:** Profile before optimizing. Our intuition about where time was
spent was wrong by an order of magnitude. Without profiling, we would have spent
effort optimizing the bit assembly loop (saving <1%) instead of addressing the
FrameV2 construction (saving 60-90%).

See [PROFILING_RESULTS.md](PROFILING_RESULTS.md) for the full 16-config breakdown.

## Phase 4: Targeted Optimization

With profiling data showing exactly where time went, three brainstorming agents
proposed optimizations from different angles: FrameV2-focused, hot-loop-focused,
and architectural. All three converged on the same priorities.

### What we implemented

**1. FrameV2 detail level setting (biggest impact)**

A three-level user setting:

- **Full:** All 10 FrameV2 fields. Default. Complete data table display.
- **Minimal:** 5 fields needed by audio HLAs (`slot`, `data`, `frame_number`,
  `short_slot`, `bitclock_error`). Sufficient for WAV export and audio streaming.
- **Off:** No FrameV2 output. Maximum speed. V1 Frame still emitted for bubble
  text and CSV. Emits an advisory warning that HLAs won't receive data.

The minimal set was determined by auditing both HLAs -- 5 of the 10 fields
(`severity`, `extra_slot`, `missed_data`, `missed_frame_sync`, `low_sample_rate`)
are never read by either HLA. They exist only for the Logic 2 protocol table.

**2. Marker density setting (second biggest impact)**

- **All bits:** Per-bit clock arrows + data dots. Current behavior.
- **Slot boundaries:** One marker per slot start. 16x fewer for 16-bit slots.
- **None:** No markers. Fastest.

**3. Batch CommitResults per frame**

Moved CommitResults + ReportProgress from per-slot to per-TDM-frame. Trivial
change, negligible measured impact from profiling but may matter more in the
real SDK where CommitResults could trigger internal processing.

**4. Structural cleanup**

- Vector capacity reserved upfront (prevents first-frame reallocation)
- Member variables reordered for cache locality (hot decode state contiguous)

### What we considered but did not implement

- **Two-pass decode:** Infeasible with the SDK's forward-only channel API
- **Clock advance by sample count:** Too risky -- crystal drift would accumulate
  and corrupt decode after a few seconds
- **GetNextBit inlining:** Virtual dispatch on SDK methods limits the benefit to
  0-1%, not worth the readability cost
- **Counted bit loop with hoisted FS check:** Changes error detection semantics
  for malformed frames -- not worth the <1% gain

## Phase 5: Measured Results

| Config | Default | Minimal+Slot | Off+None | Speedup |
|--------|--------:|-------------:|---------:|--------:|
| Stereo 16-bit | 1.2x RT | 2.3x RT | 3.0x RT | **1.9-2.5x** |
| 8-channel 16-bit | 0.3x RT | 0.5x RT | 0.8x RT | **1.5-2.3x** |
| 8-channel 32-bit | 0.2x RT | 0.3x RT | 0.4x RT | **1.6-2.2x** |

For the primary use case (realtime audio streaming), **Minimal + Slot Markers**
gives 1.5-1.9x speedup while retaining full HLA support and slot-level
waveform annotation.

See [OPTIMIZATION_RESULTS.md](OPTIMIZATION_RESULTS.md) for the full mode comparison.

## Phase 6: Post-Optimization Profile (Where Does Time Go Now?)

After implementing the FrameV2 and marker settings, we re-profiled to see what's
left to optimize. The picture changes dramatically depending on the mode.

### Minimal + Slot Markers (recommended for realtime audio)

Stereo 16-bit, 10000 frames:

| Section | Time (ms) | % of decode | Per-call (us) |
|---------|----------:|------------:|--------------:|
| GetNextBit (total) | 56.2 | 57% | 0.176 |
| - ChannelAdvance (SDK) | 25.2 | 26% | 0.079 |
| - Clock advances + overhead | 31.0 | 32% | 0.097 |
| AnalyzeTdmSlot (total) | 32.9 | 33% | - |
| - FrameV2 (5 fields) | 29.2 | 30% | 1.459 |
| - AddFrame (V1) | 1.0 | 1% | 0.049 |
| Commit (per frame) | 0.2 | <1% | 0.023 |

8-channel 16-bit, 10000 frames:

| Section | Time (ms) | % of decode | Per-call (us) |
|---------|----------:|------------:|--------------:|
| GetNextBit (total) | 229.0 | 57% | 0.179 |
| - ChannelAdvance (SDK) | 100.2 | 25% | 0.078 |
| AnalyzeTdmSlot (total) | 139.7 | 35% | - |
| - FrameV2 (5 fields) | 123.7 | 31% | 1.546 |

Even at Minimal, FrameV2 is still 30% (5 map insertions at ~1.5 us) and
GetNextBit is 57% (SDK channel operations + clock advances).

### Off + No Markers (maximum speed)

Stereo 16-bit, 10000 frames:

| Section | Time (ms) | % of decode | Per-call (us) |
|---------|----------:|------------:|--------------:|
| GetNextBit (total) | 52.1 | 82% | 0.163 |
| - ChannelAdvance (SDK) | 24.6 | 39% | 0.077 |
| - Clock advances + overhead | 27.5 | 44% | - |
| AnalyzeTdmSlot (total) | 2.0 | 3% | 0.100 |
| AddFrame (V1) | 1.0 | 2% | 0.050 |

With everything turned off, **82% of time is in GetNextBit** -- and about half
of that is the SDK's AdvanceToAbsPosition/GetBitState calls which are not
optimizable from the analyzer side.

### Are We at the Floor?

Likely yes, for this profiling approach. The remaining costs are:

1. **SDK channel operations** (~25-40% depending on mode) -- these are
   AdvanceToAbsPosition, GetBitState, AdvanceToNextEdge calls on the SDK's
   AnalyzerChannelData interface. We cannot optimize inside them.

2. **Clock edge advances** (~30%) -- the two AdvanceToNextEdge calls per bit in
   GetNextBit. These are also SDK operations.

3. **FrameV2 at Minimal** (~30%) -- still 5 map insertions per slot. The only
   way to reduce further would be to pack fields (e.g., flags integer instead
   of 2 booleans), which would require HLA changes.

4. **Profiling instrumentation itself** -- at 320K-49M calls per run, the two
   `chrono::steady_clock::now()` reads per TDM_PROFILE_SCOPE add substantial
   overhead. The ~3x profiling overhead means we can't distinguish "real work"
   from "measurement cost" at the Off+None level where individual function
   calls take 0.1-0.2 us.

### Profiling Method Limitations

The current instrumentation uses `std::chrono::steady_clock` in RAII scoped
timers (see `src/TdmProfiler.h`). This approach was chosen for simplicity and
portability, but has known limitations:

- **High per-measurement cost.** Two `steady_clock::now()` calls per scope
  entry/exit. On Windows, this typically resolves to `QueryPerformanceCounter`
  at ~20-40 ns per call, so each profiled scope adds ~40-80 ns of overhead.
  At 320K calls (stereo 16-bit), that's ~13-25 ms of pure measurement cost
  within the profiled 98 ms decode -- roughly 15-25% of the measurement is
  measuring itself.

- **Cannot profile sub-function sections accurately.** Nested scopes (e.g.,
  GetNextBit containing GetNextBit::ChannelAdvance) double-count the inner
  scope's chrono overhead. The outer scope's time includes the inner scope's
  measurement cost.

- **Unsuitable for sub-microsecond resolution.** Individual GetNextBit calls
  take 0.16-0.18 us in the fastest mode. The chrono overhead per scope is
  0.04-0.08 us -- 25-50% of the measured value is noise.

**To go deeper, a different approach is needed:**

- **Sampling profiler** (Intel VTune, Linux perf, macOS Instruments) -- zero
  instrumentation overhead, statistical function-level attribution, can see
  into SDK calls
- **RDTSC-based counters** -- hardware timestamp counter, ~1 ns resolution,
  ~5 ns overhead per read (vs ~30 ns for chrono)
- **Differential benchmarking** -- time entire runs with specific code sections
  `#ifdef`-ed out, measure the difference. No per-call overhead.

For the purposes of this optimization pass, the chrono-based profiling was
sufficient to identify the dominant bottleneck (FrameV2 at 60-90%) and validate
the fix (1.5-2.5x speedup). Further micro-optimization below the SDK call
level would require the more precise tools listed above.

## What We Learned

### About the code
1. **Output dominates decode.** The analyzer spends 60-90% of its time telling
   Logic 2 what it found, and <1% actually finding it. Any future optimization
   work should focus on the output path.

2. **FrameV2 is dramatically more expensive than V1 Frame.** 100-1500x more
   expensive per call. This is inherent to the FrameV2 API design (string-keyed
   map insertions vs fixed-field struct copy). The V1 Frame is the right choice
   for high-frequency data; FrameV2 adds flexibility at significant cost.

3. **AddMarker adds up.** At 0.08 us per call and millions of calls per capture,
   per-bit markers contribute 8-16% of decode time. Most are never rendered
   since the user only sees a small window of the waveform at a time.

4. **The test mock is not the real SDK.** The FrameV2 capture mock's std::vector
   of std::maps has different scaling characteristics than Logic 2's internal
   storage. Absolute timings from the benchmark should not be taken as
   production performance -- only relative comparisons between modes are valid.

### About the process
5. **Profile before optimizing.** Our intuition was wrong. Without data, we
   would have optimized the wrong thing.

6. **Tests enable optimization.** The 58-test safety net gave confidence to
   restructure the hot path. The FrameV2 capture mock built for testing became
   essential infrastructure for measuring the optimization.

7. **Adversarial auditing finds real gaps.** Three independent agents found the
   FrameV2 blind spot that manual review missed. The approach of launching
   multiple agents with different perspectives (coverage, assertion quality,
   mutation testing) was more effective than a single thorough review.

8. **Make speed a user choice, not a forced tradeoff.** Rather than permanently
   removing FrameV2 fields (which would break future HLAs), we made verbosity
   a setting. Users who need complete diagnostics get them; users who need
   realtime performance choose the minimal set.

## Phase 7: HLA (Python) Profiling and Optimization

The LLA (C++) feeds decoded frames to the HLA (Python) which packs PCM audio
and streams it over TCP or writes WAV files. Even a fast LLA is useless if the
HLA can't keep up.

### HLA profiling infrastructure

Added `time.perf_counter_ns()` instrumentation to both HLAs, gated by the
`TDM_HLA_PROFILE=1` environment variable (zero overhead when off). A new
`tdm-test-harness profile` CLI command drives the HLA outside Logic 2 and
reports per-section timing breakdown.

### Baseline (before optimization)

Measured with the original per-frame struct.pack, per-frame ring buffer append,
and function-call _as_signed:

| Config | Throughput | Realtime | decode/call | pack/call | ring/call |
|--------|-----------|----------|------------|----------:|----------:|
| Stereo 16-bit | 167K fps | 3.5x | 1.01 us | 0.31 us | 0.17 us |
| Stereo 32-bit | 135K fps | 2.8x | 1.43 us | 0.32 us | 0.16 us |
| 8-ch 16-bit | 41K fps | 0.8x | 1.00 us | 0.55 us | 0.17 us |
| 16-ch 16-bit | 20K fps | 0.4x | 0.93 us | 0.92 us | 0.19 us |

8-channel was barely at realtime, 16-channel was well below. The bottleneck was
the sheer volume of decode() calls (384K/sec for 8-ch) with per-frame overhead
in struct.pack and ring buffer operations.

### Optimizations applied

1. **Batch PCM packing:** Accumulate N frames (buffer_size / 2) in a
   pre-allocated bytearray via `struct.pack_into`, flush to ring buffer as a
   single chunk. Reduced ring buffer operations from 48K/sec to ~750/sec (64x).

2. **Inlined sign conversion:** Pre-computed mask/threshold/subtract constants
   in __init__, inlined bitwise ops in decode() instead of calling _as_signed()
   per slot.

3. **Cached frame.data reference:** One dict attribute lookup instead of two.

4. **Guarded sample rate derivation:** Skips function call after rate is derived.

### After optimization

| Config | Throughput | Realtime | decode/call | pack/call | ring/call |
|--------|-----------|----------|------------|----------:|----------:|
| Stereo 16-bit | 350K fps | **7.3x** | 1.14 us | 0.42 us | 1.59 us* |
| Stereo 32-bit | 237K fps | **4.9x** | 1.68 us | 0.45 us | 2.18 us* |
| 8-ch 16-bit | 73K fps | **1.5x** | 1.24 us | 0.72 us | 3.53 us* |
| 16-ch 16-bit | 40K fps | **0.8x** | 1.11 us | 1.12 us | 4.95 us* |

*Ring per-call is higher because it now processes 64 frames per call (batch).

### Comparison

| Config | Before | After | Speedup |
|--------|-------:|------:|--------:|
| Stereo 16-bit | 3.5x RT | 7.3x RT | **2.1x** |
| Stereo 32-bit | 2.8x RT | 4.9x RT | **1.8x** |
| 8-ch 16-bit | 0.8x RT | 1.5x RT | **1.9x** |
| 16-ch 16-bit | 0.4x RT | 0.8x RT | **2.0x** |

The batch packing optimization roughly doubled HLA throughput across all configs.
8-channel went from below realtime (0.8x) to comfortably above (1.5x).
16-channel improved from 0.4x to 0.8x -- still below realtime.

### Where HLA time goes now

After optimization, the dominant cost is the per-slot `decode()` call overhead
(~1.1 us). This is the cost of Logic 2 calling into Python for each slot frame.
With 384K calls/sec for 8-channel or 768K for 16-channel, even 1 us per call
consumes the entire realtime budget.

The pack and ring operations are now efficient (batched), and the sign conversion
is inlined. The remaining decode() overhead is:
- Python function call dispatch
- Dict lookups on frame.data
- Set membership test
- Attribute lookups on self

These are fundamental Python interpreter costs. Further optimization at the
Python level would yield diminishing returns.

### Future: C extension for decode hot path

The most impactful next step would be moving the `decode()` hot path to a C
extension module. The inner loop (extract slot/frame_num from frame.data, check
slot membership, apply sign conversion, accumulate sample, detect frame boundary,
pack PCM) is ~10 operations that could be a single C function call receiving a
batch of frames. This would eliminate the 1 us per-call Python overhead entirely,
potentially achieving 10-50x throughput improvement for high-channel-count
configurations.

This is tracked as a future optimization opportunity.

## Document Index

| Document | What it contains |
|----------|-----------------|
| [BENCHMARK_BASELINE.md](BENCHMARK_BASELINE.md) | Raw throughput: MSVC vs GCC, 16 configs, WSL2 vs native Windows |
| [PROFILING_RESULTS.md](PROFILING_RESULTS.md) | Per-function timing breakdown, 16 configs, 9 instrumented sections |
| [OPTIMIZATION_RESULTS.md](OPTIMIZATION_RESULTS.md) | Mode comparison: Full/Minimal/Off x All/Slot/None, 3 configs |
| [TESTING.md](TESTING.md) | Test architecture, 58 tests across 7 files, audit history |
| [../CLAUDE.md](../CLAUDE.md) | Project-level: build commands, settings, critical patterns |
