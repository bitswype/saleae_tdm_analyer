# C Extension Design for HLA decode() Hot Path

## Problem

The Python HLA decode() function is called once per TDM slot frame by Logic 2.
At 8 channels and 48 kHz, that's 384,000 calls/second. Each call has ~1.1 us of
Python interpreter overhead (function dispatch, dict lookups, attribute access)
that cannot be eliminated within Python. At 16 channels, the HLA drops below
realtime (0.8x).

## Approach: Three Levels

We evaluate three implementation approaches in order of effort, applying the
same 64-test correctness oracle (`tests/test_hla_decode.py`) to each.

### Level 1: Cython (chosen first)

Write Python-like code with C type annotations. The Cython compiler generates
C code that bypasses the Python interpreter for typed operations while retaining
seamless Python interop for dict access and object handling.

**Pros:** Readable, maintainable, incremental (can type-annotate one variable at
a time), automatic reference counting, direct access to Python objects.

**Cons:** Still goes through Python C API for dict lookups on frame.data. Speedup
limited to 2-5x for this workload (interpreter overhead eliminated, but API
calls remain).

**Estimated effort:** Low. The decode() body is ~30 lines.

### Level 2: cffi

Write the hot loop in plain C, called from Python via cffi. Python marshals
frame data into a C-friendly struct or buffer, calls the C function, and handles
the result.

**Pros:** Full control over the C code, no Python API calls in the hot path,
can use fixed-width types and manual memory layout. Potentially 5-20x speedup.

**Cons:** Two-language maintenance. Must marshal Python dicts into C structs at
the boundary. Build complexity (compile C for each platform).

**Estimated effort:** Medium. Need to define C structs, write the C function,
and write the Python marshaling layer.

### Level 3: Raw Python C Extension

Implement using the Python C API directly (`PyObject*`, `PyDict_GetItemString`,
manual `Py_INCREF`/`Py_DECREF`).

**Pros:** Maximum performance, no intermediate layers, can batch multiple frames
in a single C call if Logic 2's API allows it.

**Cons:** Maximum complexity. Manual reference counting is error-prone. Debugging
is painful. Marginal gain over cffi for this workload.

**Estimated effort:** High. Not recommended unless cffi proves insufficient.

## Scope: Narrow, Medium, Wide

Independent of the implementation approach, we choose how much of the pipeline
moves to C:

### Narrow scope

Move only the sign conversion + PCM packing inner loop (`_enqueue_frame` body).
Python still handles frame.data extraction, slot filtering, boundary detection.

**What moves to C:** `samples = [accum.get(s, 0) for s in slot_list]` +
`struct.pack_into(fmt, buf, offset, *samples)`

**Impact:** Eliminates struct.pack overhead (~0.4-1.1 us/frame boundary). Does
NOT eliminate the 1.1 us/call decode() overhead since Python still handles every
incoming frame.

**Verdict:** Insufficient. The bottleneck is the per-call overhead, not the
per-frame packing.

### Medium scope (chosen)

Move the entire decode() body to C: extract fields from frame.data, check slot
membership, detect frame boundary, apply sign conversion, accumulate sample,
and pack into batch buffer. Python handles TCP (send thread), handshake, and
thread management.

**What moves to C:** Everything from `frame.data['slot']` through
`self._accum[slot] = v`, including `_try_flush` and `_enqueue_frame` logic.

**What stays in Python:** TCP accept/send threads, handshake protocol, ring
buffer (producer side moves to C, consumer stays in Python), shutdown, sample
rate derivation (only runs on first few frames).

**Impact:** Eliminates the 1.1 us/call Python overhead on the hot path. The C
function receives a Python frame object, extracts what it needs via C API, and
does all decode work in native code. Expected 5-10x speedup on the decode path.

**Verdict:** Best balance of impact and complexity. Targets the exact bottleneck
(per-call interpreter overhead) without requiring C reimplementation of TCP
threading.

### Wide scope

Move everything including the ring buffer, sender thread, and TCP to C. Python
is just the thin HLA entry point.

**What moves to C:** The entire TdmAudioStream class minus __init__ and Logic 2
settings injection.

**Impact:** Maximum performance but massive implementation effort. The ring
buffer and TCP send are already efficient (batched, ~750 calls/sec). Moving
them to C saves <1% of total time.

**Verdict:** Not justified. The TCP/ring path is already fast enough.

## Decision

**Cython + Medium scope** as the first implementation. This gives us:

- The decode() hot path in compiled C (eliminating ~1.1 us/call overhead)
- Python-readable source (Cython is a superset of Python)
- Seamless interop with Logic 2's Python HLA framework
- The 64-test oracle validates correctness
- If Cython isn't fast enough, we can drop to cffi or raw C for the same scope

## Correctness Oracle

Any implementation must pass all 64 tests in `tests/test_hla_decode.py`:

```bash
pytest tests/test_hla_decode.py -v
```

The tests cover every branch of decode() including C-specific edge cases:
- Negative integer masking (Python arbitrary precision vs C fixed-width)
- Data values exceeding bit_depth range (masked, not truncated)
- Missing dict keys (default to 0, not crash)
- Ring buffer overflow (oldest dropped, not crash)
- Large frame numbers (beyond uint32 range)
- Zero-delta sample rate (no divide by zero)
- Negative values surviving PCM pack/unpack round-trip

## Build and Distribution

Cython requires compilation per platform. Options:
- **Logic 2 bundled Python:** Cython .pyx compiles to .c, which compiles to .so/.pyd. Ship the compiled extension alongside the HLA .py files.
- **Fallback:** If the compiled extension is not available (wrong platform, missing compiler), fall back to the pure Python decode() path. This ensures the HLA always works, just slower without the extension.

The fallback pattern:
```python
try:
    from _decode_fast import decode_fast
    _USE_CYTHON = True
except ImportError:
    _USE_CYTHON = False
```
