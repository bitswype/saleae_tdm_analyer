# Tools

Companion tools for the TDM Analyzer. None of these require Logic 2 or Saleae
hardware to install or run (except the benchmark scripts, which need Logic 2).

## Installable CLI tools

| Tool | What it does | Install |
|------|-------------|---------|
| [tdm-audio-bridge](tdm-audio-bridge/) | Plays decoded TDM audio in real time. Connects to the Audio Stream HLA's TCP server and outputs to any audio device. CLI + tkinter GUI. | `pip install tools/tdm-audio-bridge/` |
| [tdm-test-harness](tdm-test-harness/) | Drives the Audio Stream HLA outside Logic 2 for automated testing. Generates test signals, verifies decode accuracy, runs quality sweeps. | `pip install tools/tdm-test-harness/` |

## Standalone scripts

| Script | What it does |
|--------|-------------|
| [benchmark_logic2.py](benchmark_logic2.py) | Automation API benchmark - measures `add_analyzer()` and `export_data_table()` timing across configurations. Requires Logic 2 with automation enabled. |
| [prepare_benchmark_captures.py](prepare_benchmark_captures.py) | Generates .sal capture files with different analyzer settings by patching the ZIP meta.json. Sets `showInDataTable=false` and `streamToTerminal=false` to eliminate UI overhead. |

## Quick start

```bash
# Play live audio from Logic 2
pip install tools/tdm-audio-bridge/
tdm-audio-bridge gui                  # tkinter GUI
tdm-audio-bridge listen               # CLI (headless)
tdm-audio-bridge devices              # list audio output devices

# Test the HLA without Logic 2
pip install tools/tdm-test-harness/
tdm-test-harness verify --signal sine:440 --duration 0.5 --json
tdm-test-harness quality-sweep        # 11 automated test configurations
tdm-test-harness signals              # list available signal types
```
