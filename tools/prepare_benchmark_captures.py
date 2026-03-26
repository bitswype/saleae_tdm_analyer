#!/usr/bin/env python3
"""
Prepare .sal capture files for benchmarking with UI display options disabled.

Takes a source .sal file (with a TDM analyzer already configured) and produces
multiple copies, each with a different FrameV2/Marker configuration. All copies
preserve showInDataTable=false and streamToTerminal=false from the source to
eliminate UI rendering overhead from measurements.

The .sal format is a ZIP containing binary channel data and a meta.json.
This script clones the source analyzer config (preserving exact schema) and
only patches the performance-related setting values.

Requires: the source .sal must have exactly one TDM analyzer configured with
showInDataTable=false and streamToTerminal=false (set in Logic 2 UI first).

Usage:
    python tools/prepare_benchmark_captures.py <source.sal> [output_dir]
"""

import copy
import json
import os
import sys
import zipfile
from pathlib import Path


# Must match TdmAnalyzerSettings.h
FV2_FULL = 0
FV2_MINIMAL = 1
FV2_OFF = 2
MARKERS_ALL = 0
MARKERS_SLOT_ONLY = 1
MARKERS_NONE = 2

CONFIGS = [
    ("Full+All",     FV2_FULL,    MARKERS_ALL),
    ("Full+Slot",    FV2_FULL,    MARKERS_SLOT_ONLY),
    ("Full+None",    FV2_FULL,    MARKERS_NONE),
    ("Minimal+All",  FV2_MINIMAL, MARKERS_ALL),
    ("Minimal+Slot", FV2_MINIMAL, MARKERS_SLOT_ONLY),
    ("Minimal+None", FV2_MINIMAL, MARKERS_NONE),
    ("Off+All",      FV2_OFF,     MARKERS_ALL),
    ("Off+Slot",     FV2_OFF,     MARKERS_SLOT_ONLY),
    ("Off+None",     FV2_OFF,     MARKERS_NONE),
]

# Setting titles we need to patch (must match TdmAnalyzerSettings.cpp exactly)
FV2_SETTING = "Data Table / HLA Output"
MARKER_SETTING = "Waveform Markers"


def patch_analyzer(analyzer_template, config_name, fv2_detail, marker_density):
    """Clone an analyzer config and patch only the performance settings."""
    analyzer = copy.deepcopy(analyzer_template)
    analyzer["name"] = config_name
    analyzer["showInDataTable"] = False
    analyzer["streamToTerminal"] = False

    for setting in analyzer["settings"]:
        if setting["title"] == FV2_SETTING:
            setting["setting"]["value"] = fv2_detail
        elif setting["title"] == MARKER_SETTING:
            setting["setting"]["value"] = marker_density

    return analyzer


def create_benchmark_sal(source_sal, output_sal, analyzer_template, config_name,
                         fv2_detail, marker_density, orig_data_table):
    """Create a .sal file with patched analyzer config."""
    patched = patch_analyzer(analyzer_template, config_name, fv2_detail, marker_density)

    with zipfile.ZipFile(source_sal, 'r') as zin:
        meta = json.loads(zin.read('meta.json'))
        meta['data']['analyzers'] = [patched]
        meta['data']['highLevelAnalyzers'] = []
        # Preserve original dataTable config (columns disabled)
        if orig_data_table is not None:
            meta['data']['dataTable'] = orig_data_table

        with zipfile.ZipFile(output_sal, 'w', compression=zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                if item.filename == 'meta.json':
                    zout.writestr(item, json.dumps(meta))
                else:
                    zout.writestr(item, zin.read(item.filename))


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <source.sal> [output_dir]")
        sys.exit(1)

    source = Path(sys.argv[1])
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else source.parent / "benchmark_captures"
    output_dir.mkdir(parents=True, exist_ok=True)

    if not source.exists():
        print(f"ERROR: {source} not found")
        sys.exit(1)

    # Extract the template analyzer from the source .sal
    with zipfile.ZipFile(source, 'r') as z:
        meta = json.loads(z.read('meta.json'))

    data = meta['data']
    analyzers = data.get('analyzers', [])
    if not analyzers:
        print("ERROR: source .sal has no analyzers. Configure a TDM analyzer in")
        print("Logic 2 with 'Show in data table' and 'Stream to terminal' unchecked,")
        print("then save the capture.")
        sys.exit(1)

    template = analyzers[0]
    orig_data_table = data.get('dataTable')

    # Validate UI flags
    if template.get('showInDataTable', True):
        print("WARNING: source analyzer has showInDataTable=true.")
        print("For accurate benchmarks, uncheck this in Logic 2 and re-save.")
    if template.get('streamToTerminal', True):
        print("WARNING: source analyzer has streamToTerminal=true.")
        print("For accurate benchmarks, uncheck this in Logic 2 and re-save.")

    print(f"Source:   {source}")
    print(f"Template: {template['type']} analyzer '{template['name']}'")
    print(f"Output:   {output_dir}")
    print(f"Configs:  {len(CONFIGS)}")
    print()

    for name, fv2, mkr in CONFIGS:
        out_path = output_dir / f"bench_{name.replace('+', '_')}.sal"
        create_benchmark_sal(source, out_path, template, name, fv2, mkr, orig_data_table)
        size = out_path.stat().st_size
        print(f"  {name:<16} -> {out_path.name} ({size:,} bytes)")

    print(f"\nDone. {len(CONFIGS)} benchmark captures ready.")


if __name__ == "__main__":
    main()
