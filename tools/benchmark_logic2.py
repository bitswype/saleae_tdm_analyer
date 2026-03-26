#!/usr/bin/env python3
"""
Logic 2 Automation Benchmark - Real SDK Performance Measurement

Uses the Saleae Logic 2 Automation API to run simulation captures with the TDM
analyzer under various settings and measure real decode throughput. This gives
ground-truth numbers for the actual SDK operations (AnalyzerChannelData, FrameV2,
AddFrameV2) that our mock-based C++ benchmark can only approximate.

Requirements:
    pip install logic2-automation
    Logic 2 must be installed (tested with v27.x)

Usage:
    python tools/benchmark_logic2.py                    # full sweep
    python tools/benchmark_logic2.py --quick            # quick smoke test
    python tools/benchmark_logic2.py --config minimal   # single config
    python tools/benchmark_logic2.py --duration 10      # longer captures
    python tools/benchmark_logic2.py --launch            # auto-launch Logic 2
    python tools/benchmark_logic2.py --json              # machine-readable output
"""

import argparse
import csv
import io
import json
import os
import sys
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional

try:
    from saleae import automation
except ImportError:
    print("ERROR: logic2-automation not installed. Run: pip install logic2-automation")
    sys.exit(1)


# ---------------------------------------------------------------------------
# TDM analyzer setting values
# ---------------------------------------------------------------------------
# These must match the UI titles and NumberList values in TdmAnalyzerSettings.cpp

# FrameV2Detail enum values (from TdmAnalyzerSettings.h)
FV2_FULL = 0
FV2_MINIMAL = 1
FV2_OFF = 2

# MarkerDensity enum values
MARKERS_ALL = 0
MARKERS_SLOT_ONLY = 1
MARKERS_NONE = 2

# Shift order (AnalyzerEnums)
MSB_FIRST = 0  # AnalyzerEnums::MsbFirst
LSB_FIRST = 1

# Data valid edge
NEG_EDGE = 0  # AnalyzerEnums::NegEdge
POS_EDGE = 1  # AnalyzerEnums::PosEdge

# Data alignment
LEFT_ALIGNED = 0
RIGHT_ALIGNED = 1

# Bit alignment (DSP mode)
DSP_MODE_A = 0
DSP_MODE_B = 1

# Sign
UNSIGNED = 0
SIGNED = 1

# Frame sync
FS_NOT_INVERTED = 0
FS_INVERTED = 1

# Export file type
EXPORT_CSV = 0


ANALYZER_NAME = "TDM"


# ---------------------------------------------------------------------------
# Configuration presets
# ---------------------------------------------------------------------------

@dataclass
class BenchmarkConfig:
    """One benchmark configuration to run."""
    name: str
    slots_per_frame: int = 2
    bits_per_slot: int = 16
    data_bits_per_slot: int = 16
    frame_rate: int = 48000
    framev2_detail: int = FV2_FULL
    marker_density: int = MARKERS_ALL
    advanced_analysis: bool = False
    duration_seconds: float = 5.0
    digital_sample_rate: int = 10_000_000

    def analyzer_settings(self, clock_ch=0, frame_ch=1, data_ch=2):
        """Return settings dict for automation API add_analyzer()."""
        return {
            "CLOCK channel": clock_ch,
            "FRAME": frame_ch,
            "DATA": data_ch,
            "Frame Rate (Audio Sample Rate) Hz": self.frame_rate,
            "Number of slots (channels) per TDM frame (slots/frame)": self.slots_per_frame,
            "Number of bits per slot in the TDM frame": self.bits_per_slot,
            "Audio Bit Depth (data bits/slot, must be <= bits/slot)": self.data_bits_per_slot,
            "DATA Significant Bit": MSB_FIRST,
            "Data Valid CLOCK edge": NEG_EDGE,
            "DATA Bits Alignment": LEFT_ALIGNED,
            "DATA Bits Shift relative to Frame Sync": DSP_MODE_A,
            "Signed/Unsigned": UNSIGNED,
            "Frame Sync Inverted": FS_NOT_INVERTED,
            "Select export file type (TXT/CSV will actually be this file type)": EXPORT_CSV,
            "Advanced analysis of TDM signals": self.advanced_analysis,
            "Data Table / HLA Output": self.framev2_detail,
            "Waveform Markers": self.marker_density,
        }

    @property
    def fv2_label(self):
        return {FV2_FULL: "Full", FV2_MINIMAL: "Minimal", FV2_OFF: "Off"}[self.framev2_detail]

    @property
    def marker_label(self):
        return {MARKERS_ALL: "All", MARKERS_SLOT_ONLY: "Slot", MARKERS_NONE: "None"}[self.marker_density]

    @property
    def label(self):
        return f"{self.name} ({self.slots_per_frame}ch/{self.bits_per_slot}b, FV2={self.fv2_label}, Mkr={self.marker_label})"


def make_preset_configs(duration: float) -> list:
    """Build the full benchmark sweep: 9 LLA configs x channel/bitdepth combos."""
    configs = []

    # 3x3 FV2/Marker grid with stereo 16-bit (baseline comparison)
    for fv2, fv2_name in [(FV2_FULL, "Full"), (FV2_MINIMAL, "Minimal"), (FV2_OFF, "Off")]:
        for mkr, mkr_name in [(MARKERS_ALL, "All"), (MARKERS_SLOT_ONLY, "Slot"), (MARKERS_NONE, "None")]:
            configs.append(BenchmarkConfig(
                name=f"stereo-16b-{fv2_name}-{mkr_name}",
                slots_per_frame=2,
                bits_per_slot=16,
                data_bits_per_slot=16,
                framev2_detail=fv2,
                marker_density=mkr,
                duration_seconds=duration,
            ))

    # Channel scaling with optimized settings (Minimal+Slot)
    for ch in [4, 8, 16, 32]:
        configs.append(BenchmarkConfig(
            name=f"{ch}ch-16b-Minimal-Slot",
            slots_per_frame=ch,
            bits_per_slot=16,
            data_bits_per_slot=16,
            framev2_detail=FV2_MINIMAL,
            marker_density=MARKERS_SLOT_ONLY,
            duration_seconds=duration,
        ))

    # Bit depth scaling with optimized settings
    for bits in [24, 32]:
        configs.append(BenchmarkConfig(
            name=f"stereo-{bits}b-Minimal-Slot",
            slots_per_frame=2,
            bits_per_slot=bits,
            data_bits_per_slot=bits,
            framev2_detail=FV2_MINIMAL,
            marker_density=MARKERS_SLOT_ONLY,
            duration_seconds=duration,
        ))

    return configs


def make_quick_configs(duration: float) -> list:
    """Quick smoke test: just 3 configs."""
    return [
        BenchmarkConfig(
            name="stereo-16b-Full-All",
            slots_per_frame=2, bits_per_slot=16, data_bits_per_slot=16,
            framev2_detail=FV2_FULL, marker_density=MARKERS_ALL,
            duration_seconds=duration,
        ),
        BenchmarkConfig(
            name="stereo-16b-Minimal-Slot",
            slots_per_frame=2, bits_per_slot=16, data_bits_per_slot=16,
            framev2_detail=FV2_MINIMAL, marker_density=MARKERS_SLOT_ONLY,
            duration_seconds=duration,
        ),
        BenchmarkConfig(
            name="stereo-16b-Off-None",
            slots_per_frame=2, bits_per_slot=16, data_bits_per_slot=16,
            framev2_detail=FV2_OFF, marker_density=MARKERS_NONE,
            duration_seconds=duration,
        ),
    ]


NAMED_CONFIGS = {
    "full": lambda d: [BenchmarkConfig(
        name="stereo-Full-All", framev2_detail=FV2_FULL,
        marker_density=MARKERS_ALL, duration_seconds=d)],
    "minimal": lambda d: [BenchmarkConfig(
        name="stereo-Minimal-Slot", framev2_detail=FV2_MINIMAL,
        marker_density=MARKERS_SLOT_ONLY, duration_seconds=d)],
    "off": lambda d: [BenchmarkConfig(
        name="stereo-Off-None", framev2_detail=FV2_OFF,
        marker_density=MARKERS_NONE, duration_seconds=d)],
}


# ---------------------------------------------------------------------------
# Benchmark runner
# ---------------------------------------------------------------------------

@dataclass
class BenchmarkResult:
    config_name: str
    config_label: str
    slots: int
    bits: int
    fv2_detail: str
    markers: str
    capture_duration_s: float
    wall_time_s: float
    analyzer_time_s: float
    total_frames: int
    frames_per_sec: float
    bits_per_sec: float
    megabits_per_sec: float
    realtime_ratio: float  # >1 means faster than realtime
    error: Optional[str] = None


def run_one_benchmark(manager, config: BenchmarkConfig, sim_device_id: str,
                      export_dir: Path, verbose: bool = True) -> BenchmarkResult:
    """Run a single benchmark configuration and return results."""

    if verbose:
        print(f"\n{'='*60}")
        print(f"  {config.label}")
        print(f"{'='*60}")

    # Calculate expected bit rate for this config
    expected_bitclock = config.frame_rate * config.slots_per_frame * config.bits_per_slot
    # Use a sample rate that's at least 4x the bitclock for clean simulation
    sample_rate = max(config.digital_sample_rate, expected_bitclock * 4)
    # Round up to a common Logic 2 sample rate
    common_rates = [500_000, 1_000_000, 2_000_000, 5_000_000,
                    10_000_000, 12_500_000, 25_000_000, 50_000_000,
                    100_000_000, 200_000_000, 500_000_000]
    for rate in common_rates:
        if rate >= sample_rate:
            sample_rate = rate
            break

    if verbose:
        print(f"  Bitclock: {expected_bitclock/1e6:.1f} MHz, Sample rate: {sample_rate/1e6:.0f} MS/s")

    device_config = automation.LogicDeviceConfiguration(
        enabled_digital_channels=[0, 1, 2],
        digital_sample_rate=sample_rate,
    )

    capture_config = automation.CaptureConfiguration(
        capture_mode=automation.TimedCaptureMode(
            duration_seconds=config.duration_seconds,
        ),
    )

    export_path = export_dir / f"{config.name}.csv"

    try:
        # Start capture and time everything
        t_start = time.monotonic()

        with manager.start_capture(
            device_id=sim_device_id,
            device_configuration=device_config,
            capture_configuration=capture_config,
        ) as capture:
            if verbose:
                print(f"  Capture started... waiting {config.duration_seconds}s")

            capture.wait()
            t_capture_done = time.monotonic()

            if verbose:
                capture_wall = t_capture_done - t_start
                print(f"  Capture done ({capture_wall:.1f}s wall). Adding analyzer...")

            # Add the TDM analyzer with our settings
            analyzer = capture.add_analyzer(
                ANALYZER_NAME,
                label=f"Bench-{config.name}",
                settings=config.analyzer_settings(),
            )

            if verbose:
                print(f"  Analyzer added. Exporting data table (blocks until processing complete)...")

            t_analyzer_start = time.monotonic()

            # Export data table - this blocks until the analyzer has finished processing
            capture.export_data_table(
                filepath=str(export_path),
                analyzers=[analyzer],
            )

            t_done = time.monotonic()

        # Count exported rows
        total_frames = 0
        if export_path.exists():
            with open(export_path, 'r') as f:
                reader = csv.reader(f)
                header = next(reader, None)  # skip header
                total_frames = sum(1 for _ in reader)

        wall_time = t_done - t_start
        analyzer_time = t_done - t_analyzer_start
        capture_time = t_capture_done - t_start

        # Throughput
        fps = total_frames / analyzer_time if analyzer_time > 0 else 0
        bits_total = total_frames * config.bits_per_slot
        bps = bits_total / analyzer_time if analyzer_time > 0 else 0
        mbps = bps / 1_000_000

        # Realtime ratio: how fast vs expected frame rate
        # Expected frames = frame_rate * slots_per_frame * duration
        # But we measure actual frames from export
        expected_fps = config.frame_rate * config.slots_per_frame
        rt_ratio = fps / expected_fps if expected_fps > 0 else 0

        result = BenchmarkResult(
            config_name=config.name,
            config_label=config.label,
            slots=config.slots_per_frame,
            bits=config.bits_per_slot,
            fv2_detail=config.fv2_label,
            markers=config.marker_label,
            capture_duration_s=config.duration_seconds,
            wall_time_s=round(wall_time, 2),
            analyzer_time_s=round(analyzer_time, 2),
            total_frames=total_frames,
            frames_per_sec=round(fps, 0),
            bits_per_sec=round(bps, 0),
            megabits_per_sec=round(mbps, 2),
            realtime_ratio=round(rt_ratio, 2),
        )

        if verbose:
            print(f"  Frames decoded: {total_frames:,}")
            print(f"  Analyzer time:  {analyzer_time:.2f}s")
            print(f"  Throughput:     {fps:,.0f} frames/s = {mbps:.2f} Mbit/s")
            print(f"  RT ratio:       {rt_ratio:.2f}x")

        # Clean up export file
        if export_path.exists():
            export_path.unlink()

        return result

    except Exception as e:
        if verbose:
            print(f"  ERROR: {e}")
        return BenchmarkResult(
            config_name=config.name,
            config_label=config.label,
            slots=config.slots_per_frame,
            bits=config.bits_per_slot,
            fv2_detail=config.fv2_label,
            markers=config.marker_label,
            capture_duration_s=config.duration_seconds,
            wall_time_s=0, analyzer_time_s=0, total_frames=0,
            frames_per_sec=0, bits_per_sec=0, megabits_per_sec=0,
            realtime_ratio=0, error=str(e),
        )


def find_simulation_device(manager) -> Optional[str]:
    """Find a simulation device, preferring Logic Pro 16."""
    devices = manager.get_devices(include_simulation_devices=True)
    sim_devices = [d for d in devices if d.is_simulation]

    if not sim_devices:
        return None

    # Prefer the Logic Pro 16 simulation (F4241)
    for d in sim_devices:
        if d.device_id == 'F4241':
            return d.device_id

    return sim_devices[0].device_id


def print_summary_table(results: list):
    """Print a formatted summary table."""
    print(f"\n{'='*90}")
    print("  BENCHMARK RESULTS - Real SDK Performance (Logic 2 Simulation)")
    print(f"{'='*90}")
    print(f"  {'Config':<35} {'Frames':>10} {'Time':>7} {'Mbit/s':>8} {'RT':>6}")
    print(f"  {'-'*35} {'-'*10} {'-'*7} {'-'*8} {'-'*6}")

    for r in results:
        if r.error:
            print(f"  {r.config_label:<35} {'ERROR':>10} {r.error}")
        else:
            print(f"  {r.config_label:<35} {r.total_frames:>10,} {r.analyzer_time_s:>6.1f}s {r.megabits_per_sec:>8.2f} {r.realtime_ratio:>5.1f}x")

    print(f"  {'-'*35} {'-'*10} {'-'*7} {'-'*8} {'-'*6}")
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark TDM analyzer with real Logic 2 SDK via simulation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--launch", action="store_true",
                        help="Auto-launch Logic 2 (default: connect to running instance)")
    parser.add_argument("--port", type=int, default=10430,
                        help="Logic 2 automation port (default: 10430)")
    parser.add_argument("--quick", action="store_true",
                        help="Quick smoke test (3 configs instead of full sweep)")
    parser.add_argument("--config", choices=list(NAMED_CONFIGS.keys()),
                        help="Run a single named configuration")
    parser.add_argument("--duration", type=float, default=5.0,
                        help="Capture duration in seconds (default: 5.0)")
    parser.add_argument("--json", action="store_true",
                        help="Output results as JSON")
    parser.add_argument("--output", type=str, default=None,
                        help="Write results to file (JSON or CSV based on extension)")
    parser.add_argument("--quiet", action="store_true",
                        help="Suppress per-config output")
    args = parser.parse_args()

    # Build config list
    if args.config:
        configs = NAMED_CONFIGS[args.config](args.duration)
    elif args.quick:
        configs = make_quick_configs(args.duration)
    else:
        configs = make_preset_configs(args.duration)

    # Create temp export directory
    repo_root = Path(__file__).resolve().parent.parent
    export_dir = repo_root / "tmp" / "benchmark_exports"
    export_dir.mkdir(parents=True, exist_ok=True)

    verbose = not args.quiet

    if verbose:
        print(f"Logic 2 Automation Benchmark")
        print(f"Configs: {len(configs)}, Duration: {args.duration}s each")
        print(f"Export dir: {export_dir}")

    # Connect to Logic 2
    try:
        if args.launch:
            if verbose:
                print("Launching Logic 2...")
            manager = automation.Manager.launch()
        else:
            if verbose:
                print(f"Connecting to Logic 2 on port {args.port}...")
            manager = automation.Manager.connect(port=args.port)
    except Exception as e:
        print(f"\nERROR: Could not connect to Logic 2: {e}")
        print("\nMake sure Logic 2 is running with automation enabled:")
        print("  1. Open Logic 2")
        print("  2. Go to Preferences (gear icon)")
        print("  3. Scroll to bottom, enable 'Automation' checkbox")
        print("  4. Re-run this script")
        print("\nOr use --launch to auto-start Logic 2.")
        sys.exit(1)

    try:
        # Find simulation device
        sim_id = find_simulation_device(manager)
        if not sim_id:
            # If no simulation devices but we have real hardware, we could
            # use that too, but simulation is safer for benchmarking
            devices = manager.get_devices()
            if devices:
                print(f"\nNo simulation devices found, but real device available: {devices[0].device_id}")
                print("Using real device (connect TDM signals or expect decode errors)")
                sim_id = devices[0].device_id
            else:
                print("\nERROR: No devices found (real or simulated)")
                print("Enable simulation devices in Logic 2 preferences.")
                sys.exit(1)

        if verbose:
            print(f"Using device: {sim_id}")
            app_info = manager.get_app_info()
            print(f"Logic 2 version: {app_info.application_version if hasattr(app_info, 'application_version') else 'unknown'}")

        # Run benchmarks
        results = []
        for i, config in enumerate(configs):
            if verbose:
                print(f"\n[{i+1}/{len(configs)}]", end="")
            result = run_one_benchmark(manager, config, sim_id, export_dir, verbose)
            results.append(result)

        # Summary
        if not args.json:
            print_summary_table(results)

        # JSON output
        if args.json:
            print(json.dumps([asdict(r) for r in results], indent=2))

        # File output
        if args.output:
            out_path = Path(args.output)
            if out_path.suffix == '.json':
                with open(out_path, 'w') as f:
                    json.dump([asdict(r) for r in results], f, indent=2)
            elif out_path.suffix == '.csv':
                with open(out_path, 'w', newline='') as f:
                    if results:
                        writer = csv.DictWriter(f, fieldnames=asdict(results[0]).keys())
                        writer.writeheader()
                        for r in results:
                            writer.writerow(asdict(r))
            if verbose:
                print(f"Results written to {out_path}")

    finally:
        manager.close()

    # Clean up empty export dir
    try:
        export_dir.rmdir()
    except OSError:
        pass  # not empty, that's fine

    # Exit with error if any configs failed
    if any(r.error for r in results):
        sys.exit(1)


if __name__ == "__main__":
    main()
