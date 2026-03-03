# Phase 8: HLA Scaffold & Settings - Research

**Researched:** 2026-03-02
**Domain:** Saleae Logic 2 High Level Analyzer Python extension structure
**Confidence:** HIGH

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| REQ-01 | HLA lives in `hla/` subdirectory of this repository | Directory structure pattern confirmed — standard single-dir layout |
| REQ-02 | `hla/` contains a valid `extension.json` so Logic 2 can load it | extension.json schema fully documented with exact required fields |
| REQ-03 | User installs by adding `hla/` as a custom extension directory in Logic 2 preferences | UI flow confirmed: Extensions panel → three-dots menu → "Load Existing Extension" → select extension.json |
| REQ-04 | HLA appears in Logic 2 as "TDM WAV Export" in the analyzer chain | The `name` field in extensions.json top-level and extensions key controls the displayed name |
| REQ-05 | HLA exposes a `slots` setting — comma-separated or range notation | Use `StringSetting(label='Slots (e.g. 0,2,4 or 0-3)')` — parsing happens in `__init__` |
| REQ-06 | HLA exposes an `output_path` setting — absolute path to the desired WAV file | Use `StringSetting(label='Output Path (absolute)')` |
| REQ-07 | HLA exposes a `bit_depth` setting — 16 or 32 bit (default 16) | Use `ChoicesSetting(['16', '32'], label='Bit Depth')` with `.default = '16'` |
</phase_requirements>

## Summary

The Logic 2 HLA system requires exactly two files per extension: `extension.json` (metadata and entry point registration) and a Python source file containing the `HighLevelAnalyzer` subclass. Settings are declared as class-level attributes using `StringSetting`, `NumberSetting`, and `ChoicesSetting` from `saleae.analyzers`. At runtime, Logic 2 injects the configured values as instance attributes before `__init__` runs, making them accessible as `self.setting_name`. The `decode(self, frame: AnalyzerFrame)` method is called for every frame the upstream LLA emits.

The TDM LLA already emits FrameV2 frames with a well-defined schema. From reading `TdmAnalyzer.cpp`, every normal slot frame has type `"slot"` and carries ten fields in `frame.data`: `slot` (int), `data` (int, signed-adjusted), `frame_number` (int), `severity` (str: `"ok"/"warning"/"error"`), `short_slot` (bool), `extra_slot` (bool), `bitclock_error` (bool), `missed_data` (bool), `missed_frame_sync` (bool), `low_sample_rate` (bool). The advisory frame has type `"advisory"` with `severity` and `message` fields. This schema is critical for writing the HLA's decode logic.

For Phase 8 (scaffold only), the `decode()` method can return `None` for all frames — the goal is to verify the extension loads cleanly. The settings must be declared at class level so Logic 2 can surface them in the UI. The `output_path` and `slots` settings must be `StringSetting` because they accept free-form text; `bit_depth` must be `ChoicesSetting` to constrain valid inputs to `"16"` and `"32"`. Defaults cannot be set via constructor kwargs; they require the `.default` attribute pattern post-construction.

**Primary recommendation:** Create `hla/extension.json` + `hla/TdmWavExport.py` with the exact schema shown below. Verify the HLA loads in Logic 2 by adding it via Extensions panel → three-dots → "Load Existing Extension" and selecting the `extension.json` file.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `saleae.analyzers` | bundled with Logic 2 | HLA base class, settings types, AnalyzerFrame | Only officially supported HLA API |
| Python `wave` | stdlib 3.8 | WAV file writing (Phase 9) | In Logic 2's embedded Python stdlib |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Python `struct` | stdlib 3.8 | Binary packing if manual WAV needed | Fallback only — `wave` module handles PCM |
| Python `os.path` | stdlib 3.8 | Path validation for `output_path` setting | Check absolute path in `__init__` |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `StringSetting` for bit_depth | `NumberSetting(min_value=16, max_value=32)` | ChoicesSetting constrains to exactly 16 or 32, no intermediate values accepted |
| `ChoicesSetting` for slots | `StringSetting` | StringSetting is correct here — slots require free-form parse |

**Installation:** No installation needed. `saleae.analyzers` is provided by Logic 2's embedded Python 3.8 runtime. The extension is loaded from disk, not installed via pip.

## Architecture Patterns

### Recommended Project Structure
```
hla/
├── extension.json        # Extension metadata and entry point registration
└── TdmWavExport.py       # Single Python file containing the HLA class
```

No subdirectories needed for Phase 8. Both files must live in the same directory — Logic 2 loads the Python file relative to `extension.json` using the module name in `entryPoint`.

### Pattern 1: extension.json Format

**What:** JSON manifest registering the extension with Logic 2.
**Required fields:** `name`, `apiVersion`, `version`, `author`, `extensions` object with at least one entry.
**When to use:** Exactly this format — no variation.

```json
{
    "name": "TDM WAV Export",
    "apiVersion": "1.0.0",
    "version": "1.0.0",
    "author": "Chris Keeser",
    "description": "Exports selected TDM slots to a WAV file via Logic 2 HLA.",
    "extensions": {
        "TDM WAV Export": {
            "type": "HighLevelAnalyzer",
            "entryPoint": "TdmWavExport.TdmWavExport"
        }
    }
}
```

**Key points:**
- `apiVersion` must be `"1.0.0"` — this is the only supported value as of 2024
- `entryPoint` format is `"<module_filename_without_py>.<ClassName>"`
- The key under `extensions` (here `"TDM WAV Export"`) is the name shown in Logic 2's analyzer chain
- `name` at the top level is the extension package name in the Extensions panel

### Pattern 2: HLA Python Class Skeleton

**What:** Minimal valid HLA class that loads without errors.
**When to use:** Phase 8 deliverable — this is the scaffold.

```python
# Source: Official saleae.analyzers API + verified examples from
# github.com/saleae/hla-i2c-8-bit-display and github.com/LeonardMH/SaleaeSocketTransportHLA

from saleae.analyzers import HighLevelAnalyzer, AnalyzerFrame, StringSetting, NumberSetting, ChoicesSetting


class TdmWavExport(HighLevelAnalyzer):
    """Logic 2 High Level Analyzer: export selected TDM slots to WAV."""

    # Settings declared as class-level attributes.
    # Logic 2 reads these before instantiating the class and builds the UI from them.
    # Values are injected as instance attributes before __init__ runs.

    slots = StringSetting(label='Slots (e.g. 0,2,4 or 0-3)')
    output_path = StringSetting(label='Output Path (absolute path to .wav file)')
    bit_depth = ChoicesSetting(['16', '32'], label='Bit Depth')

    # Set default for bit_depth after construction — no default= kwarg in ChoicesSetting
    bit_depth.default = '16'

    result_types = {
        'status': {
            'format': '{{data.message}}'
        }
    }

    def __init__(self):
        """
        Called once before decode() starts.
        Setting values are available as self.slots, self.output_path, self.bit_depth.
        """
        # Phase 8: parse and validate settings, initialize state
        # (Full implementation in Phase 9)
        self._slots_raw = self.slots        # str, e.g. "0,2,4" or "0-3"
        self._output_path = self.output_path  # str, absolute path
        self._bit_depth = int(self.bit_depth) # str "16" or "32" -> int

    def decode(self, frame: AnalyzerFrame):
        """
        Called for every frame emitted by the upstream LLA.
        Return None, a single AnalyzerFrame, or a list of AnalyzerFrames.
        Phase 8: return None (no-op) to verify scaffold loads cleanly.
        """
        return None
```

### Pattern 3: How Logic 2 Injects Settings

**What:** Settings declared as class attributes are populated by Logic 2's runtime before `__init__` is called.
**Critical:** Do NOT try to initialize state in the class body. Initialize in `__init__` only.

```python
# CORRECT: settings accessed in __init__ as self.setting_name
def __init__(self):
    self._output_path = self.output_path  # self.output_path is already a str value here

# WRONG: settings not yet available at class definition time
class TdmWavExport(HighLevelAnalyzer):
    some_state = self.output_path  # NameError — self doesn't exist at class body level
```

### Pattern 4: The `entryPoint` Module Resolution

**What:** Logic 2 imports the Python file named before the dot in `entryPoint`.
**Rule:** The filename (without `.py`) must exactly match the module part of `entryPoint`.

```
entryPoint: "TdmWavExport.TdmWavExport"
              ^^^^^^^^^^^^^  ^^^^^^^^^^^^
              module = TdmWavExport.py  class = TdmWavExport
```

### Anti-Patterns to Avoid

- **Relative imports in HLA Python file:** The HLA file runs in Logic 2's embedded Python context with `hla/` as the working directory. Standard relative imports work only for sibling files in `hla/`. Do not import from the project root or `src/`.
- **Using `NumberSetting` for bit_depth:** `NumberSetting` accepts any float in range; user could enter 24 which the WAV writer won't support. Use `ChoicesSetting` to constrain to exactly `"16"` or `"32"`.
- **Non-absolute output_path:** Logic 2's working directory is unpredictable. Relative paths in `output_path` will resolve to somewhere inside the Logic 2 app bundle, not the user's home directory. The `__init__` method must validate the path is absolute.
- **ChoicesSetting with keyword-only choices list:** From the API docs, `ChoicesSetting(choices, **kwargs)` — `choices` is the first positional argument. The correct form is `ChoicesSetting(['16', '32'], label='Bit Depth')`, NOT `ChoicesSetting(choices=['16', '32'], label='Bit Depth')` (untested whether kwargs form works).
- **settings appearing in random order:** Logic 2 does NOT guarantee settings appear in source order in the UI. Do not rely on UI ordering for UX.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV file creation (Phase 9) | Custom binary writer | Python `wave` module | Handles header, chunk sizes, sample encoding; already in stdlib 3.8 |
| HLA base class | Custom frame protocol | `HighLevelAnalyzer` from `saleae.analyzers` | Only officially supported interface; custom would need reverse-engineering |
| Settings UI | Custom dialog or web UI | `StringSetting`/`ChoicesSetting` class attributes | Logic 2 renders UI automatically from class-level setting declarations |

**Key insight:** The Python `wave` module from stdlib handles all WAV PCM writing including header management. The existing C++ WAV handler logic in `TdmAnalyzerResults.cpp` is a guide but should NOT be ported to Python — use `wave.open()` instead.

## Common Pitfalls

### Pitfall 1: Missing `result_types` in class body
**What goes wrong:** If `result_types` is absent, Logic 2 may fail to display HLA output frames or crash silently when `decode()` returns an `AnalyzerFrame`.
**Why it happens:** The HLA engine checks `result_types` to format frame display text.
**How to avoid:** Always declare `result_types` even in the scaffold, even if `decode()` returns `None`.
**Warning signs:** HLA loads but no frames appear in data table despite `decode()` returning values.

### Pitfall 2: Wrong `entryPoint` format in extension.json
**What goes wrong:** Logic 2 fails to load the extension with a cryptic Python import error.
**Why it happens:** The module portion must exactly match the `.py` filename (case-sensitive on Linux/macOS).
**How to avoid:** File is `TdmWavExport.py`, class is `TdmWavExport`, entryPoint is `"TdmWavExport.TdmWavExport"`.
**Warning signs:** Extension appears in Extensions panel but cannot be added to analyzer chain.

### Pitfall 3: Third-party packages not available
**What goes wrong:** `import numpy` or similar fails with `ModuleNotFoundError`.
**Why it happens:** Logic 2 uses its own embedded Python 3.8, not the system Python. Only stdlib is reliably available.
**How to avoid:** Phase 8-10 uses only stdlib (`wave`, `struct`, `os`, `os.path`). No third-party imports needed.
**Warning signs:** ImportError on any non-stdlib package at HLA load time.

### Pitfall 4: Settings not optional — empty string is valid but must be handled
**What goes wrong:** A `StringSetting` left blank by the user passes an empty string `""` to `__init__`. Code that does `open(self.output_path, ...)` crashes with `FileNotFoundError` on empty string.
**Why it happens:** Logic 2 does not enforce non-empty for `StringSetting`.
**How to avoid:** In `__init__`, check `if not self.output_path.strip()` and store a sentinel; handle gracefully in `decode()`. REQ-16 requires this error to surface as an error frame.
**Warning signs:** HLA crashes on first frame when output_path is empty.

### Pitfall 5: `ChoicesSetting` default value syntax
**What goes wrong:** `ChoicesSetting(['16', '32'], default='16')` raises `TypeError` because `default` is not a recognized keyword parameter.
**Why it happens:** The `default` attribute must be set post-construction: `bit_depth.default = '16'`.
**How to avoid:** Set `.default` as a separate statement after the setting is declared.
**Warning signs:** Logic 2 shows no default pre-selected in UI dropdown, or TypeError at import.

### Pitfall 6: Advisory frames from LLA
**What goes wrong:** The TDM LLA emits an `"advisory"` frame type (not `"slot"`) when sample rate is low. An HLA that assumes all frames are type `"slot"` will crash or silently corrupt on advisory frames.
**Why it happens:** `TdmAnalyzer.cpp` emits `AddFrameV2(advisory, "advisory", 0, 0)` before any slot frames if `mLowSampleRate` is true.
**How to avoid:** In `decode()`, check `if frame.type != 'slot': return None` before processing frame data.
**Warning signs:** HLA crashes immediately on first frame when sample rate is below 4x bit clock.

## Code Examples

Verified patterns from official sources and LLA source:

### The Exact FrameV2 Schema from the TDM LLA

```python
# Source: /src/TdmAnalyzer.cpp (read directly in research)
# Every normal slot frame has:
#   frame.type == "slot"
#   frame.data["slot"]              -> int, 0-based slot index
#   frame.data["data"]              -> int, signed-adjusted sample value
#   frame.data["frame_number"]      -> int, TDM frame count
#   frame.data["severity"]          -> str: "ok", "warning", or "error"
#   frame.data["short_slot"]        -> bool
#   frame.data["extra_slot"]        -> bool
#   frame.data["bitclock_error"]    -> bool
#   frame.data["missed_data"]       -> bool
#   frame.data["missed_frame_sync"] -> bool
#   frame.data["low_sample_rate"]   -> bool
#   frame.start_time                -> SaleaeTime object
#   frame.end_time                  -> SaleaeTime object

# Advisory frame (when sample rate is below 4x bit clock):
#   frame.type == "advisory"
#   frame.data["severity"]  -> "warning"
#   frame.data["message"]   -> human-readable description string
```

### Minimal decode() Implementation for Phase 8

```python
# Source: pattern from saleae/hla-i2c-8-bit-display, adapted for TDM schema
def decode(self, frame: AnalyzerFrame):
    # Skip advisory frames — they carry no audio data
    if frame.type != 'slot':
        return None
    # Phase 8: no-op. Phase 9 adds WAV writing here.
    return None
```

### Loading the Extension in Logic 2 (UI Steps)

1. Open Logic 2
2. Click the **Extensions** panel icon (right sidebar)
3. Click the **three-dots (...)** menu icon in the Extensions panel header
4. Select **"Load Existing Extension..."**
5. Navigate to `hla/extension.json` and select it
6. The extension appears in the Extensions list as "TDM WAV Export"
7. Add a TDM analyzer to a capture, then add "TDM WAV Export" as a downstream HLA

**To reload after editing Python source:**
Right-click the HLA instance in the Analyzers panel → "Reload Source Files"

### `result_types` Format String Syntax

```python
# Source: saleae/hla-i2c-8-bit-display (verified)
result_types = {
    'status': {
        'format': '{{data.message}}'   # double-brace — accesses frame.data["message"]
    },
    'error': {
        'format': 'Error: {{data.message}}'
    }
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Manual `get_capabilities()` / `set_settings()` methods | Class-level setting attributes (`StringSetting`, etc.) | ~2021 (Logic 2.x) | Old API is gone; class attribute pattern is the only supported form |
| FrameV1 (opaque, single-value) | FrameV2 (named fields via `AddString/AddInteger/AddBoolean`) | Logic 2.3.43 (2022) | HLA can now receive rich multi-field frames from custom C++ LLAs |
| HLA only chainable from built-in analyzers | Custom C++ LLAs with `UseFrameV2()` can be HLA sources | Logic 2.3.43 (2022) | This project depends on this feature |

**Deprecated/outdated:**
- Old `get_capabilities()` / `set_settings()` pattern: replaced by class-attribute settings; do not use
- The gist at gist.github.com/Marcus10110 shows the pre-2021 API — do not follow it

## Open Questions

1. **Settings order in Logic 2 UI**
   - What we know: Logic 2 does NOT guarantee settings appear in source-code order in the UI panel
   - What's unclear: Whether any ordering mechanism exists (e.g., declaration order, alphabetical)
   - Recommendation: Design settings to be independently understandable; don't rely on visual ordering

2. **ChoicesSetting `default` behavior in Logic 2 UI**
   - What we know: `.default` attribute can be set post-construction; Logic 2 may pre-select it
   - What's unclear: Whether Logic 2 actually uses `.default` for UI pre-selection or just as a code fallback
   - Recommendation: In `__init__`, always use `self.bit_depth or '16'` as a Python-side fallback in addition to setting `.default`

3. **Whether `frame.start_time` subtraction works for sample-rate derivation**
   - What we know: `frame.start_time` and `frame.end_time` are `SaleaeTime` objects, subtractable
   - What's unclear: Exact arithmetic to derive TDM frame rate from timestamps (needed in Phase 9)
   - Recommendation: Phase 9 research item — not needed for Phase 8 scaffold

## Sources

### Primary (HIGH confidence)
- `/src/TdmAnalyzer.cpp` (read directly) — exact FrameV2 schema with all field names and types
- `saleae.github.io/apidocs/api_analyzers.html` — official API docs for `HighLevelAnalyzer`, `AnalyzerFrame`, `StringSetting`, `NumberSetting`, `ChoicesSetting` with constructor signatures
- `raw.githubusercontent.com/saleae/hla-i2c-transactions/master/extension.json` — real extension.json example, confirmed format
- `raw.githubusercontent.com/saleae/hla-i2c-8-bit-display/master/HighLevelAnalyzer.py` — complete verified HLA skeleton with `result_types`, `__init__`, and `decode()`

### Secondary (MEDIUM confidence)
- `github.com/LeonardMH/SaleaeSocketTransportHLA` — community HLA with file I/O and ChoicesSetting patterns; confirms class-level attribute style and `entryPoint` format
- `discuss.saleae.com/t/hla-settings-default-values/3475` — confirms `.default` attribute pattern post-construction; no `default=` kwarg in constructor
- `discuss.saleae.com/t/optional-hla-settings/2534` — confirms StringSetting accepts empty string; settings are not inherently required
- `discuss.saleae.com/t/hla-python-import/2266` — confirms Python 3.8 embedded, no third-party packages without workarounds

### Tertiary (LOW confidence)
- WebSearch result describing "three-dots → Load Existing Extension → select extension.json" UI flow — not verified against current Logic 2 version (UI may have changed in recent releases)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — confirmed from official API docs and real working examples
- Architecture: HIGH — verified extension.json format and Python class structure from official repos
- FrameV2 schema: HIGH — read directly from project source (`TdmAnalyzer.cpp`)
- Pitfalls: MEDIUM — most confirmed from official forum posts; settings ordering unconfirmed
- Load UI flow: MEDIUM — confirmed from forum posts; exact UI may vary by Logic 2 version

**Research date:** 2026-03-02
**Valid until:** 2026-09-01 (Saleae HLA API is stable; apiVersion "1.0.0" unchanged for 3+ years)
