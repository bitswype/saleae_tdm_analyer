# Contributing to TDM Analyzer

Thanks for considering a contribution. Whether it's a bug fix, a new feature, better docs, or just a question - you're welcome here.

This project is a work in progress, and so is this contributing guide. If anything is confusing or missing, that's a bug in the docs, not a problem with you. Please open an issue or just ask.

---

## Getting Started

### Prerequisites

**C++ (LLA plugin):**
- CMake 3.13+
- A C++11-compatible compiler:
  - Linux: GCC 5+ (`sudo apt install build-essential cmake`)
  - macOS: Xcode command line tools (`xcode-select --install`)
  - Windows: Visual Studio 2017+ with "Desktop development with C++"

**Python (HLAs and tools):**
- Python 3.8+
- pip (for installing the tools in editable mode)
- PortAudio (for tdm-audio-bridge playback - `sudo apt install portaudio19-dev` on Linux)
- sox (optional, for audio quality analysis)

### Clone and build

```bash
git clone git@github.com:bitswype/saleae_tdm_analyer.git
cd saleae_tdm_analyer

# Build the C++ LLA
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install the Python tools (editable mode, so changes take effect immediately)
pip install -e tools/tdm-test-harness/
pip install -e tools/tdm-audio-bridge/
```

The Saleae AnalyzerSDK is fetched automatically by CMake - no manual download needed.

### Verify everything works

```bash
# Run the stress/reliability tests (no Logic 2 or hardware needed)
python3 tests/test_stress_reliability.py

# Run the test harness verification suite
tdm-test-harness verify --signal sine:440 --duration 0.5 --json
```

If both pass, you're good to go.

---

## Project Layout

This is a multi-language project with distinct subsystems. You don't need to understand all of them to contribute to one.

| Component | Language | What it does |
|-----------|----------|-------------|
| `src/` | C++ | Low Level Analyzer plugin - decodes raw TDM signals |
| `hla-wav-export/` | Python | HLA that writes selected audio slots to WAV files |
| `hla-audio-stream/` | Python | HLA that streams live audio over TCP |
| `tools/tdm-test-harness/` | Python | Standalone test/verification tool (no Logic 2 needed) |
| `tools/tdm-audio-bridge/` | Python | Companion app that plays streamed audio (CLI + GUI) |
| `tests/` | Python | Stress and reliability test suite |

Each component can be worked on independently. The C++ LLA produces FrameV2 data that the Python HLAs consume, and the tools communicate over TCP - the boundaries are clean.

---

## Making Changes

### Code style

**C++:** A `.clang-format` file is in the repo root. Key points:
- Allman brace style
- 4-space indentation
- 140-character line limit

If you have clang-format installed, you can format before committing:
```bash
clang-format -i src/*.cpp src/*.h
```

**Python:** No formal formatter configured yet. Keep it readable and consistent with the surrounding code.

### Commit messages

We use semantic versioning and care about readable git history:

- One logical change per commit
- Commit messages should explain *why*, not just *what* (the diff shows what changed)
- Non-trivial commits should have a body with context
- Conventional prefixes are encouraged: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`

Example:
```
feat: add 32-bit PCM support to audio stream HLA

The existing implementation only handled 16-bit samples. 32-bit is
needed for professional audio interfaces that capture at higher bit
depths. The TCP protocol handshake now includes bit_depth so the
receiver knows how to unpack the PCM data.
```

### Running tests

Before submitting, make sure the tests pass:

```bash
# Stress/reliability tests
python3 tests/test_stress_reliability.py

# Signal verification (quick check)
tdm-test-harness verify --signal sine:440 --duration 0.5 --json

# Full quality sweep (11 configurations, takes longer)
tdm-test-harness quality-sweep
```

The test harness runs entirely without Logic 2 or real hardware. It drives the HLA code directly using a fake frame emitter, so you can test the full audio pipeline from your development machine.

---

## Submitting Changes

1. **Fork the repo** and create a branch from `main`
2. **Make your changes** with clear, atomic commits
3. **Run the tests** and confirm they pass
4. **Open a pull request** against `main` with a clear description of what and why

### What makes a good PR

- **Focused scope.** A PR that does one thing well is easier to review and more likely to be merged quickly.
- **Clear description.** What problem does this solve? How did you approach it? Any trade-offs?
- **Tests for new behavior.** If you're adding a feature or fixing a bug, a test that covers it is very welcome.
- **Draft PRs are welcome.** If you're unsure about an approach, open a draft early. It's better to get feedback after 20 lines than after 200.

### CI

GitHub Actions builds the C++ LLA on all three platforms (Windows, macOS x86_64 + arm64, Linux) on every push and PR. You'll see the results on your PR automatically.

---

## Areas Where Help is Welcome

Not sure where to start? Here are some areas that could use attention:

- **Documentation improvements** - the README is comprehensive but could always be clearer
- **Test coverage** - more edge cases, more configurations, more confidence
- **Cross-platform testing** - especially macOS and Windows, where the maintainer has less coverage
- **New HLA features** - slot selection improvements, additional export formats
- **Bug reports** - even if you can't fix it, a clear report with reproduction steps is valuable

---

## Questions?

Open an issue. There's no such thing as a dumb question about this codebase - if something isn't clear, that's a documentation problem we'd like to fix.
