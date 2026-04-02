"""Unit tests for HLA decode() -- correctness oracle for C/Cython reimplementation.

Tests every branch of TdmAudioStream.decode() and TdmWavExport.decode() with
known inputs and expected outputs. Any reimplementation must pass all tests.
"""
import sys
import os
import socket
import struct
import json
import time
import wave
import tempfile
import math
import random
import pytest

# Add paths
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools', 'tdm-test-harness'))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'hla-audio-stream'))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'hla-wav-export'))

from tdm_test_harness.hla_driver import HlaDriver
from tdm_test_harness.frame_emitter import FakeFrame, emit_frames


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

def _pick_free_port():
    """Bind to port 0, let the OS pick a free port, then release it."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


@pytest.fixture
def port():
    return _pick_free_port()


def slot_frame(slot, data, frame_num, start_time=0.0, **extra):
    """Create a FakeFrame mimicking an LLA slot frame."""
    d = {
        'slot': slot,
        'data': data,
        'frame_number': frame_num,
        'short_slot': False,
        'bitclock_error': False,
    }
    d.update(extra)
    end_time = start_time + 0.00001
    return FakeFrame('slot', start_time, end_time, d)


def warmup(driver, sample_rate=48000, n=3):
    """Feed warmup frames to derive sample rate."""
    slots = driver._hla._slot_list
    samples = [[0] * len(slots) for _ in range(n)]
    frames = list(emit_frames(samples, sample_rate, slots))
    driver.feed(frames)


def read_pcm(port, channels, bit_depth, n_frames, timeout=5.0):
    """Connect to HLA TCP server, read handshake + PCM, return decoded samples."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect(('127.0.0.1', port))

    # Read handshake
    buf = b''
    while b'\n' not in buf:
        buf += sock.recv(4096)
    line, remainder = buf.split(b'\n', 1)
    handshake = json.loads(line)

    # Read PCM
    fmt_char = 'h' if bit_depth == 16 else 'i'
    fmt = f'<{channels}{fmt_char}'
    frame_size = channels * (2 if bit_depth == 16 else 4)
    total_bytes = n_frames * frame_size

    pcm = remainder
    while len(pcm) < total_bytes:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            pcm += chunk
        except socket.timeout:
            break

    sock.close()

    decoded = []
    for i in range(0, len(pcm) - frame_size + 1, frame_size):
        decoded.append(list(struct.unpack(fmt, pcm[i:i + frame_size])))

    return handshake, decoded


def _connect_and_wait(port, timeout=5.0):
    """Connect a TCP client to the HLA and read the handshake.

    Returns (socket, handshake_dict, remainder_bytes).
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect(('127.0.0.1', port))
    buf = b''
    while b'\n' not in buf:
        buf += sock.recv(4096)
    line, remainder = buf.split(b'\n', 1)
    handshake = json.loads(line)
    return sock, handshake, remainder


def _read_frames_from_sock(sock, channels, bit_depth, remainder=b'', timeout=2.0):
    """Read all available PCM frames from an already-connected socket."""
    fmt_char = 'h' if bit_depth == 16 else 'i'
    fmt = f'<{channels}{fmt_char}'
    frame_size = channels * (2 if bit_depth == 16 else 4)

    pcm = remainder
    sock.settimeout(timeout)
    while True:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            pcm += chunk
        except socket.timeout:
            break

    decoded = []
    for i in range(0, len(pcm) - frame_size + 1, frame_size):
        decoded.append(list(struct.unpack(fmt, pcm[i:i + frame_size])))
    return decoded


# ===========================================================================
# 1. Frame type filtering
# ===========================================================================

def test_advisory_frame_ignored(port):
    driver = HlaDriver('0,1', port=port)
    frame = FakeFrame('advisory', 0.0, 0.001, {'severity': 'warning', 'message': 'test'})
    result = driver._hla.decode(frame)
    assert result is None
    assert driver._hla._frame_count == 0
    driver.shutdown()


def test_unknown_frame_type_ignored(port):
    driver = HlaDriver('0,1', port=port)
    frame = FakeFrame('unknown_type', 0.0, 0.001, {})
    result = driver._hla.decode(frame)
    assert result is None
    driver.shutdown()


def test_slot_frame_processed(port):
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    frame = slot_frame(0, 1000, frame_num=10)
    driver._hla.decode(frame)
    assert 0 in driver._hla._accum
    assert driver._hla._accum[0] == 1000
    driver.shutdown()


# ===========================================================================
# 2. Slot filtering
# ===========================================================================

def test_unselected_slot_ignored(port):
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    frame = slot_frame(2, 9999, frame_num=10)
    driver._hla.decode(frame)
    assert 2 not in driver._hla._accum
    driver.shutdown()


def test_selected_slot_accumulated(port):
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    frame = slot_frame(1, 5000, frame_num=10)
    driver._hla.decode(frame)
    assert driver._hla._accum.get(1) == 5000
    driver.shutdown()


def test_non_contiguous_slots(port):
    driver = HlaDriver('0,3,7', port=port)
    warmup(driver)
    # Feed slots 0-7, only 0,3,7 should accumulate
    for s in range(8):
        driver._hla.decode(slot_frame(s, s * 100, frame_num=10))
    assert driver._hla._accum.get(0) == 0
    assert driver._hla._accum.get(3) == 300
    assert driver._hla._accum.get(7) == 700
    assert 1 not in driver._hla._accum
    assert 5 not in driver._hla._accum
    driver.shutdown()


def test_all_slots_filtered(port):
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    initial_count = driver._hla._frame_count
    # Feed only slots 5,6 which are not in the filter
    for i in range(100):
        driver._hla.decode(slot_frame(5, i, frame_num=10 + i))
        driver._hla.decode(slot_frame(6, i, frame_num=10 + i))
    assert driver._hla._frame_count == initial_count
    driver.shutdown()


# ===========================================================================
# 3. Sign conversion
# ===========================================================================

def test_sign_16bit_positive(port):
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x7FFF, frame_num=10))
    assert driver._hla._accum[0] == 32767
    driver.shutdown()


def test_sign_16bit_negative(port):
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x8000, frame_num=10))
    assert driver._hla._accum[0] == -32768
    driver.shutdown()


def test_sign_16bit_zero(port):
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0, frame_num=10))
    assert driver._hla._accum[0] == 0
    driver.shutdown()


def test_sign_16bit_minus_one(port):
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0xFFFF, frame_num=10))
    assert driver._hla._accum[0] == -1
    driver.shutdown()


def test_sign_32bit_positive(port):
    driver = HlaDriver('0', port=port, bit_depth=32)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x7FFFFFFF, frame_num=10))
    assert driver._hla._accum[0] == 2147483647
    driver.shutdown()


def test_sign_32bit_negative(port):
    driver = HlaDriver('0', port=port, bit_depth=32)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x80000000, frame_num=10))
    assert driver._hla._accum[0] == -2147483648
    driver.shutdown()


def test_sign_zero_crossing(port):
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x7FFF, frame_num=10))
    assert driver._hla._accum[0] == 32767
    driver._hla.decode(slot_frame(0, 0x8000, frame_num=10))
    assert driver._hla._accum[0] == -32768
    driver.shutdown()


# ===========================================================================
# 4. Error frame handling
# ===========================================================================

def test_short_slot_skipped(port):
    driver = HlaDriver('0', port=port)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 1000, frame_num=10))
    assert driver._hla._accum[0] == 1000
    # Now feed short_slot=True -- should NOT overwrite
    driver._hla.decode(slot_frame(0, 9999, frame_num=10, short_slot=True))
    assert driver._hla._accum[0] == 1000  # unchanged
    driver.shutdown()


def test_bitclock_error_skipped(port):
    driver = HlaDriver('0', port=port)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 1000, frame_num=10))
    driver._hla.decode(slot_frame(0, 9999, frame_num=10, bitclock_error=True))
    assert driver._hla._accum[0] == 1000
    driver.shutdown()


def test_both_errors_skipped(port):
    driver = HlaDriver('0', port=port)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 1000, frame_num=10))
    driver._hla.decode(slot_frame(0, 9999, frame_num=10, short_slot=True, bitclock_error=True))
    assert driver._hla._accum[0] == 1000
    driver.shutdown()


def test_error_then_normal(port):
    driver = HlaDriver('0', port=port)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 9999, frame_num=10, short_slot=True))
    assert 0 not in driver._hla._accum  # error frame, not accumulated
    driver._hla.decode(slot_frame(0, 5000, frame_num=10))
    assert driver._hla._accum[0] == 5000  # normal frame accumulated
    driver.shutdown()


def test_missing_error_fields_treated_as_ok(port):
    """Frames from Minimal FrameV2 mode may not have error fields."""
    driver = HlaDriver('0', port=port)
    warmup(driver)
    # Frame with NO short_slot or bitclock_error keys at all
    d = {'slot': 0, 'data': 4242, 'frame_number': 10}
    frame = FakeFrame('slot', 0.0, 0.001, d)
    driver._hla.decode(frame)
    assert driver._hla._accum[0] == 4242
    driver.shutdown()


# ===========================================================================
# 5. Frame boundary detection
# ===========================================================================

def test_frame_boundary_triggers_enqueue(port):
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    initial = driver._hla._frame_count
    # After warmup, _last_frame_num is 2. Feeding frame_num=10 triggers a flush
    # of the warmup residual (frame 2), then frame_num=11 triggers a flush of
    # frame 10. So _frame_count increases by 2.
    driver._hla.decode(slot_frame(0, 100, frame_num=10, start_time=10 / 48000))
    driver._hla.decode(slot_frame(1, 200, frame_num=10, start_time=10 / 48000 + 0.00001))
    driver._hla.decode(slot_frame(0, 300, frame_num=11, start_time=11 / 48000))
    assert driver._hla._frame_count == initial + 2
    driver.shutdown()


def test_same_frame_num_no_additional_enqueue(port):
    """The warmup residual flush happens on the first decode with a new frame_num,
    but subsequent decodes with the same frame_num cause no additional enqueue."""
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    initial = driver._hla._frame_count
    # The first decode (fn=10) triggers flush of warmup residual (fn=2 -> 10),
    # but after that, all same frame_num so no more enqueues.
    for i in range(4):
        driver._hla.decode(slot_frame(i % 2, i * 100, frame_num=10))
    assert driver._hla._frame_count == initial + 1
    driver.shutdown()


def test_frame_num_gap(port):
    driver = HlaDriver('0', port=port)
    warmup(driver)
    initial = driver._hla._frame_count
    # First decode (fn=10) flushes warmup residual, second (fn=15) flushes fn=10.
    driver._hla.decode(slot_frame(0, 100, frame_num=10, start_time=10 / 48000))
    driver._hla.decode(slot_frame(0, 200, frame_num=15, start_time=15 / 48000))
    assert driver._hla._frame_count == initial + 2
    driver.shutdown()


def test_first_frame_not_enqueued(port):
    """First frame ever should not trigger enqueue (no previous frame to flush)."""
    driver = HlaDriver('0', port=port)
    # No warmup -- _last_frame_num is None
    # But _sample_rate is also None, so enqueue won't happen anyway.
    # Feed the first frame
    driver._hla.decode(slot_frame(0, 100, frame_num=0, start_time=0.0))
    assert driver._hla._frame_count == 0
    assert driver._hla._last_frame_num == 0
    driver.shutdown()


# ===========================================================================
# 6. Accumulator and missing slots
# ===========================================================================

def test_missing_slot_produces_silence(port):
    """Missing slot in a TDM frame produces silence (0) in the PCM output."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)
    warmup(driver)

    # Connect TCP client before feeding data so the sender thread can deliver
    sock, hs, rem = _connect_and_wait(port)

    # Only feed slot 0 for frame 10, slot 1 is missing
    driver._hla.decode(slot_frame(0, 1234, frame_num=10, start_time=10 / 48000))
    # Trigger flush of frame 10 by starting frame 11
    driver._hla.decode(slot_frame(0, 0, frame_num=11, start_time=11 / 48000))
    driver._hla._flush_batch()
    time.sleep(0.3)

    samples = _read_frames_from_sock(sock, 2, 16, rem, timeout=1.0)
    sock.close()

    # Find the frame with slot 0 == 1234; slot 1 should be 0 (silence)
    found = [s for s in samples if s[0] == 1234]
    assert len(found) >= 1, f"Frame [1234, 0] not found in received: {samples}"
    assert found[0] == [1234, 0], f"Expected [1234, 0], got {found[0]}"
    driver.shutdown()


def test_accumulator_reset_after_flush(port):
    driver = HlaDriver('0,1', port=port)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 1000, frame_num=10, start_time=10 / 48000))
    driver._hla.decode(slot_frame(1, 2000, frame_num=10, start_time=10 / 48000))
    # Trigger flush
    driver._hla.decode(slot_frame(0, 0, frame_num=11, start_time=11 / 48000))
    # Accumulator should be reset (only slot 0 from frame 11 is in it now)
    assert 1 not in driver._hla._accum
    driver.shutdown()


def test_accumulator_overwrites_on_same_slot(port):
    driver = HlaDriver('0', port=port)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 1000, frame_num=10))
    driver._hla.decode(slot_frame(0, 2000, frame_num=10))  # overwrite
    assert driver._hla._accum[0] == 2000
    driver.shutdown()


# ===========================================================================
# 7. PCM packing correctness (TCP tests)
#
# Strategy: connect TCP client FIRST, then feed data, so the sender thread
# can deliver frames to the client.
# ===========================================================================

def test_stereo_16bit_pcm(port):
    driver = HlaDriver('0,1', port=port, bit_depth=16, buffer_size=128)
    warmup(driver)

    # Connect client first so it can receive the data
    sock, hs, rem = _connect_and_wait(port)
    assert hs['channels'] == 2
    assert hs['bit_depth'] == 16

    # Feed 5 frames of known data
    for i in range(5):
        t = (10 + i) / 48000
        driver._hla.decode(slot_frame(0, 100 * (i + 1), frame_num=10 + i, start_time=t))
        driver._hla.decode(slot_frame(1, 200 * (i + 1), frame_num=10 + i, start_time=t + 0.000001))
    # Trigger final flush
    driver._hla.decode(slot_frame(0, 0, frame_num=15, start_time=15 / 48000))
    driver._hla._flush_batch()
    time.sleep(0.3)  # let sender thread drain

    samples = _read_frames_from_sock(sock, 2, 16, rem, timeout=1.0)
    sock.close()

    # The warmup residual flush (fn=2 -> fn=10 boundary) produces a silence
    # frame [0,0] before our test data. Skip silence frames to find test data.
    data_samples = [s for s in samples if s != [0, 0]]
    assert len(data_samples) >= 5, f"Expected >= 5 data frames, got {len(data_samples)}: {samples}"
    # First 5 frames: [100,200], [200,400], [300,600], [400,800], [500,1000]
    for i in range(5):
        assert data_samples[i] == [100 * (i + 1), 200 * (i + 1)], \
            f"Frame {i}: expected {[100 * (i + 1), 200 * (i + 1)]}, got {data_samples[i]}"
    driver.shutdown()


def test_stereo_32bit_pcm(port):
    driver = HlaDriver('0,1', port=port, bit_depth=32, buffer_size=128)
    warmup(driver)

    sock, hs, rem = _connect_and_wait(port)
    assert hs['bit_depth'] == 32

    for i in range(3):
        t = (10 + i) / 48000
        driver._hla.decode(slot_frame(0, 100000 * (i + 1), frame_num=10 + i, start_time=t))
        driver._hla.decode(slot_frame(1, 200000 * (i + 1), frame_num=10 + i, start_time=t + 0.000001))
    driver._hla.decode(slot_frame(0, 0, frame_num=13, start_time=13 / 48000))
    driver._hla._flush_batch()
    time.sleep(0.3)

    samples = _read_frames_from_sock(sock, 2, 32, rem, timeout=1.0)
    sock.close()

    data_samples = [s for s in samples if s != [0, 0]]
    for i in range(3):
        assert data_samples[i] == [100000 * (i + 1), 200000 * (i + 1)]
    driver.shutdown()


def test_4channel_pcm(port):
    driver = HlaDriver('0,1,2,3', port=port, bit_depth=16, buffer_size=128)
    warmup(driver)

    sock, hs, rem = _connect_and_wait(port)

    t = 10 / 48000
    for s in range(4):
        driver._hla.decode(slot_frame(s, (s + 1) * 1000, frame_num=10, start_time=t))
    driver._hla.decode(slot_frame(0, 0, frame_num=11, start_time=11 / 48000))
    driver._hla._flush_batch()
    time.sleep(0.3)

    samples = _read_frames_from_sock(sock, 4, 16, rem, timeout=1.0)
    sock.close()

    data_samples = [s for s in samples if s != [0, 0, 0, 0]]
    assert len(data_samples) >= 1, f"Expected >= 1 data frames, got {data_samples}"
    assert data_samples[0] == [1000, 2000, 3000, 4000]
    driver.shutdown()


def test_pcm_byte_order(port):
    """Verify PCM is little-endian."""
    driver = HlaDriver('0', port=port, bit_depth=16, buffer_size=128)
    warmup(driver)

    # Connect client first
    sock, hs, rem = _connect_and_wait(port)

    driver._hla.decode(slot_frame(0, 0x0102, frame_num=10, start_time=10 / 48000))
    driver._hla.decode(slot_frame(0, 0, frame_num=11, start_time=11 / 48000))
    driver._hla._flush_batch()
    time.sleep(0.3)

    # Read raw bytes from socket
    pcm = rem
    sock.settimeout(1.0)
    while True:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            pcm += chunk
        except socket.timeout:
            break
    sock.close()

    # The warmup residual flush produces a silence frame (0x00, 0x00) first,
    # followed by our test frame (0x0102 LE = bytes [0x02, 0x01]).
    # Find the test frame bytes: skip leading zero pairs.
    offset = 0
    while offset + 1 < len(pcm) and pcm[offset] == 0 and pcm[offset + 1] == 0:
        offset += 2
    assert offset + 1 < len(pcm), "Could not find non-zero PCM data"
    # 0x0102 in little-endian 16-bit = bytes [0x02, 0x01]
    assert pcm[offset] == 0x02
    assert pcm[offset + 1] == 0x01
    driver.shutdown()


# ===========================================================================
# 8. Batch packing
# ===========================================================================

def test_partial_batch_flushed_on_shutdown(port):
    """shutdown() calls _flush_batch(), flushing any partial batch to the ring."""
    driver = HlaDriver('0', port=port, buffer_size=128)
    warmup(driver)

    # Drain warmup residual
    driver._hla.decode(slot_frame(0, 0, frame_num=3, start_time=3 / 48000))
    driver._hla._flush_batch()
    # Clear the ring so we start clean
    with driver._hla._ring_lock:
        driver._hla._ring.clear()

    # Feed 3 frames (way less than batch_size=64) and trigger boundary flush
    for i in range(3):
        t = (4 + i) / 48000
        driver._hla.decode(slot_frame(0, (i + 1) * 100, frame_num=4 + i, start_time=t))
    # Trigger boundary for last frame
    driver._hla.decode(slot_frame(0, 0, frame_num=7, start_time=7 / 48000))

    # Verify partial batch exists (not yet flushed to ring)
    assert driver._hla._batch_count > 0, "Expected partial batch before shutdown"

    # Do NOT call _flush_batch() manually -- shutdown() should do it
    driver._hla.shutdown()

    # After shutdown, batch should be flushed
    assert driver._hla._batch_count == 0, "Expected batch flushed after shutdown"


def test_full_batch_flushed(port):
    driver = HlaDriver('0', port=port, buffer_size=4)
    # batch_size = 4 // 2 = 2
    warmup(driver)
    initial_ring_len = len(driver._hla._ring)
    # Feed 3 frames to trigger one batch flush (batch_size=2)
    for i in range(3):
        t = (10 + i) / 48000
        driver._hla.decode(slot_frame(0, i, frame_num=10 + i, start_time=t))
    # Trigger flush of the third frame
    driver._hla.decode(slot_frame(0, 0, frame_num=13, start_time=13 / 48000))
    assert driver._hla._batch_count <= 1  # remainder after batch flush
    # Verify at least one ring entry exists (the flushed batch)
    assert len(driver._hla._ring) > initial_ring_len, \
        f"Expected ring to grow from {initial_ring_len}, got {len(driver._hla._ring)}"
    driver.shutdown()


def test_batch_size_matches_config(port):
    driver = HlaDriver('0', port=port, buffer_size=128)
    assert driver._hla._batch_size == 64  # 128 // 2
    driver.shutdown()

    port2 = _pick_free_port()
    driver2 = HlaDriver('0', port=port2, buffer_size=10)
    assert driver2._hla._batch_size == 5  # 10 // 2
    driver2.shutdown()


# ===========================================================================
# 9. Sample rate derivation
# ===========================================================================

def test_sample_rate_derived_from_timing(port):
    driver = HlaDriver('0', port=port)
    assert driver._hla._sample_rate is None
    # Feed 2 frames for same slot with correct timing
    driver._hla.decode(slot_frame(0, 0, frame_num=0, start_time=0.0))
    driver._hla.decode(slot_frame(0, 0, frame_num=1, start_time=1 / 48000))
    assert driver._hla._sample_rate == 48000
    driver.shutdown()


def test_sample_rate_not_derived_from_single_frame(port):
    driver = HlaDriver('0', port=port)
    driver._hla.decode(slot_frame(0, 0, frame_num=0, start_time=0.0))
    assert driver._hla._sample_rate is None
    driver.shutdown()


def test_sample_rate_clamped(port):
    driver = HlaDriver('0', port=port)
    # Feed with absurd timing (1 Hz = 1 second between frames)
    driver._hla.decode(slot_frame(0, 0, frame_num=0, start_time=0.0))
    driver._hla.decode(slot_frame(0, 0, frame_num=1, start_time=1.0))
    # Should be clamped to 48000 fallback
    assert driver._hla._sample_rate == 48000
    driver.shutdown()


# ===========================================================================
# 10. Init error handling
# ===========================================================================

def test_init_error_emitted_once(port):
    """HLA with invalid config should emit error frame once, then return None."""
    from TdmAudioStream import TdmAudioStream
    hla = TdmAudioStream.__new__(TdmAudioStream)
    hla.slots = '0,1'
    hla.tcp_port = '99999'  # invalid port
    hla.buffer_size = '128'
    hla.bit_depth = '16'
    hla.__init__()

    assert hla._init_error is not None

    frame = FakeFrame('slot', 0.0, 0.001, {'slot': 0, 'data': 0, 'frame_number': 0})
    result = hla.decode(frame)
    assert result is not None  # first call returns error frame
    assert result.type == 'error'

    result2 = hla.decode(frame)
    assert result2 is None  # subsequent calls return None


# ===========================================================================
# 11. WAV export
# ===========================================================================

def make_wav_hla(output_path, slots='0,1', bit_depth='16'):
    """Create a TdmWavExport HLA instance configured for testing."""
    from TdmWavExport import TdmWavExport
    hla = TdmWavExport.__new__(TdmWavExport)
    hla.slots = slots
    hla.bit_depth = bit_depth
    hla.output_path = output_path
    hla.__init__()
    return hla


def read_wav_samples(path):
    """Read all samples from a WAV file, return as list of frame lists."""
    with wave.open(path, 'rb') as wf:
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        n_frames = wf.getnframes()
        raw = wf.readframes(n_frames)

    fmt_char = 'h' if sampwidth == 2 else 'i'
    fmt = f'<{n_channels}{fmt_char}'
    frame_size = n_channels * sampwidth

    samples = []
    for i in range(0, len(raw) - frame_size + 1, frame_size):
        samples.append(list(struct.unpack(fmt, raw[i:i + frame_size])))
    return samples


def test_wav_basic_stereo(tmp_path):
    path = str(tmp_path / 'test.wav')
    hla = make_wav_hla(path, '0,1', '16')
    # Warmup
    for i in range(3):
        t = i / 48000
        hla.decode(slot_frame(0, 0, frame_num=i, start_time=t))
        hla.decode(slot_frame(1, 0, frame_num=i, start_time=t + 0.000001))
    # Test frames
    for i in range(5):
        fn = 3 + i
        t = fn / 48000
        hla.decode(slot_frame(0, (i + 1) * 100, frame_num=fn, start_time=t))
        hla.decode(slot_frame(1, (i + 1) * 200, frame_num=fn, start_time=t + 0.000001))
    # Flush final frame
    hla.decode(slot_frame(0, 0, frame_num=8, start_time=8 / 48000))
    hla.shutdown()

    samples = read_wav_samples(path)
    # Skip warmup frames (silence frames)
    data_samples = [s for s in samples if s != [0, 0]]
    assert len(data_samples) >= 5
    for i in range(5):
        assert data_samples[i] == [(i + 1) * 100, (i + 1) * 200]


def test_wav_32bit(tmp_path):
    path = str(tmp_path / 'test32.wav')
    hla = make_wav_hla(path, '0', '32')
    for i in range(3):
        hla.decode(slot_frame(0, 0, frame_num=i, start_time=i / 48000))
    for i in range(3):
        fn = 3 + i
        hla.decode(slot_frame(0, (i + 1) * 100000, frame_num=fn, start_time=fn / 48000))
    hla.decode(slot_frame(0, 0, frame_num=6, start_time=6 / 48000))
    hla.shutdown()

    samples = read_wav_samples(path)
    data = [s for s in samples if s != [0]]
    assert len(data) >= 3
    for i in range(3):
        assert data[i] == [(i + 1) * 100000]


def test_wav_slot_filtering(tmp_path):
    path = str(tmp_path / 'filtered.wav')
    hla = make_wav_hla(path, '1', '16')  # only slot 1
    # Warmup: feed slot 1 frames to derive sample rate.
    # Slot 0 frames are ignored by the filter, so only slot 1 is processed.
    for i in range(3):
        t = i / 48000
        hla.decode(slot_frame(1, 0, frame_num=i, start_time=t))
    # Test frames: slot 0 is ignored; only slot 1 data matters
    for i in range(3):
        fn = 3 + i
        t = fn / 48000
        hla.decode(slot_frame(1, (i + 1) * 500, frame_num=fn, start_time=t))
    # Flush the last frame by sending a new frame_num on the selected slot
    hla.decode(slot_frame(1, 0, frame_num=6, start_time=6 / 48000))
    hla.shutdown()

    with wave.open(path, 'rb') as wf:
        assert wf.getnchannels() == 1
    samples = read_wav_samples(path)
    data = [s for s in samples if s != [0]]
    assert len(data) >= 3
    for i in range(3):
        assert data[i] == [(i + 1) * 500]


def test_wav_error_frames_skipped(tmp_path):
    path = str(tmp_path / 'errors.wav')
    hla = make_wav_hla(path, '0', '16')
    for i in range(3):
        hla.decode(slot_frame(0, 0, frame_num=i, start_time=i / 48000))
    # Normal frame
    hla.decode(slot_frame(0, 1000, frame_num=3, start_time=3 / 48000))
    # Error frame -- should be skipped
    hla.decode(slot_frame(0, 9999, frame_num=4, start_time=4 / 48000, short_slot=True))
    # Another normal frame triggers flush of frame 4
    hla.decode(slot_frame(0, 2000, frame_num=5, start_time=5 / 48000))
    hla.decode(slot_frame(0, 0, frame_num=6, start_time=6 / 48000))
    hla.shutdown()

    samples = read_wav_samples(path)
    # Frame 4 had short_slot=True, so sample should be 0 (not 9999)
    data = [s[0] for s in samples]
    assert 9999 not in data  # error frame data should not appear
    assert 1000 in data
    assert 2000 in data
    # Explicitly verify the frame after 1000 is 0 (silence substitution)
    idx_1000 = data.index(1000)
    assert data[idx_1000 + 1] == 0, \
        f"Expected silence (0) after error frame, got {data[idx_1000 + 1]}"


def test_wav_batch_flush_on_shutdown(tmp_path):
    """Verify partial batch is written before WAV is closed."""
    path = str(tmp_path / 'partial.wav')
    hla = make_wav_hla(path, '0', '16')
    for i in range(3):
        hla.decode(slot_frame(0, 0, frame_num=i, start_time=i / 48000))
    # Feed just 2 data frames (less than batch_size)
    hla.decode(slot_frame(0, 1111, frame_num=3, start_time=3 / 48000))
    hla.decode(slot_frame(0, 2222, frame_num=4, start_time=4 / 48000))
    hla.decode(slot_frame(0, 0, frame_num=5, start_time=5 / 48000))
    hla.shutdown()

    samples = read_wav_samples(path)
    data = [s[0] for s in samples]
    assert 1111 in data
    assert 2222 in data


# ===========================================================================
# 12. C port safety tests
# ===========================================================================

def test_negative_data_input_16bit(port):
    """Python int -1 masked to 16-bit: (-1) & 0xFFFF = 0xFFFF -> sign convert -> -1."""
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, -1, frame_num=10))
    assert driver._hla._accum[0] == -1
    driver.shutdown()


def test_negative_data_input_32bit(port):
    """Python int -32768 masked to 32-bit: (-32768) & 0xFFFFFFFF = 0xFFFF8000 -> sign convert -> -32768."""
    driver = HlaDriver('0', port=port, bit_depth=32)
    warmup(driver)
    driver._hla.decode(slot_frame(0, -32768, frame_num=10))
    assert driver._hla._accum[0] == -32768
    driver.shutdown()


def test_negative_data_large_value(port):
    """Python int -32768 with 16-bit: (-32768) & 0xFFFF = 0x8000 -> sign convert -> -32768."""
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, -32768, frame_num=10))
    assert driver._hla._accum[0] == -32768
    driver.shutdown()


def test_data_exceeds_16bit_range(port):
    """data=0x1FFFF with 16-bit masked to 0xFFFF -> sign convert -> -1."""
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x1FFFF, frame_num=10))
    assert driver._hla._accum[0] == -1
    driver.shutdown()


def test_data_exceeds_32bit_range(port):
    """data=0x1_FFFFFFFF with 32-bit masked to 0xFFFFFFFF -> sign convert -> -1."""
    driver = HlaDriver('0', port=port, bit_depth=32)
    warmup(driver)
    driver._hla.decode(slot_frame(0, 0x1_FFFFFFFF, frame_num=10))
    assert driver._hla._accum[0] == -1
    driver.shutdown()


def test_negative_values_pcm_roundtrip(port):
    """Connect TCP, feed negative values, verify they survive PCM packing/unpacking."""
    driver = HlaDriver('0,1', port=port, bit_depth=16, buffer_size=128)
    warmup(driver)

    sock, hs, rem = _connect_and_wait(port)

    # Feed data=0x8000 (slot 0) and data=0xFFFF (slot 1)
    driver._hla.decode(slot_frame(0, 0x8000, frame_num=10, start_time=10 / 48000))
    driver._hla.decode(slot_frame(1, 0xFFFF, frame_num=10, start_time=10 / 48000 + 0.000001))
    # Trigger flush
    driver._hla.decode(slot_frame(0, 0, frame_num=11, start_time=11 / 48000))
    driver._hla._flush_batch()
    time.sleep(0.3)

    samples = _read_frames_from_sock(sock, 2, 16, rem, timeout=1.0)
    sock.close()

    found = [s for s in samples if s[0] == -32768]
    assert len(found) >= 1, f"Frame [-32768, -1] not found in received: {samples}"
    assert found[0] == [-32768, -1], f"Expected [-32768, -1], got {found[0]}"
    driver.shutdown()


def test_missing_data_key_defaults_to_zero(port):
    """Frame with no 'data' key at all should default to 0."""
    driver = HlaDriver('0', port=port, bit_depth=16)
    warmup(driver)
    d = {'slot': 0, 'frame_number': 10, 'short_slot': False, 'bitclock_error': False}
    frame = FakeFrame('slot', 0.0, 0.001, d)
    driver._hla.decode(frame)
    assert driver._hla._accum[0] == 0
    driver.shutdown()


def test_ring_buffer_overflow_drops_oldest(port):
    """Ring buffer with maxlen=4, batch_size=2. Feed 20+ frames; ring length <= 4."""
    driver = HlaDriver('0', port=port, buffer_size=4)
    # batch_size = 4 // 2 = 2
    warmup(driver)
    # Feed 20+ frames (don't connect TCP, so ring accumulates and overflows)
    for i in range(25):
        t = (10 + i) / 48000
        driver._hla.decode(slot_frame(0, i, frame_num=10 + i, start_time=t))
    # Trigger flush of the last frame
    driver._hla.decode(slot_frame(0, 0, frame_num=35, start_time=35 / 48000))
    driver._hla._flush_batch()
    assert len(driver._hla._ring) <= 4, \
        f"Ring buffer should be <= 4, got {len(driver._hla._ring)}"
    driver.shutdown()


def test_large_frame_numbers(port):
    """Frame numbers up to 2**62 should work correctly."""
    driver = HlaDriver('0', port=port)
    warmup(driver)
    large_fn = 2**62
    driver._hla.decode(slot_frame(0, 42, frame_num=large_fn, start_time=10 / 48000))
    assert driver._hla._last_frame_num == large_fn
    assert driver._hla._accum[0] == 42
    driver.shutdown()


def test_frame_number_uint32_overflow(port):
    """Frame numbers beyond 2**32 should still trigger boundary detection."""
    driver = HlaDriver('0', port=port)
    warmup(driver)
    fn1 = 2**32 + 1
    fn2 = 2**32 + 2
    driver._hla.decode(slot_frame(0, 100, frame_num=fn1, start_time=10 / 48000))
    initial = driver._hla._frame_count
    driver._hla.decode(slot_frame(0, 200, frame_num=fn2, start_time=11 / 48000))
    # fn2 != fn1, so boundary detected and frame_count should increase
    assert driver._hla._frame_count == initial + 1
    driver.shutdown()


def test_sample_rate_zero_delta_skipped(port):
    """Two frames for same slot at identical start_time should not cause ZeroDivisionError."""
    driver = HlaDriver('0', port=port)
    driver._hla.decode(slot_frame(0, 0, frame_num=0, start_time=0.0))
    driver._hla.decode(slot_frame(0, 0, frame_num=1, start_time=0.0))
    assert driver._hla._sample_rate is None
    driver.shutdown()


def test_sample_rate_clamped_high(port):
    """Very small delta (1e-9 seconds) should clamp to 48000."""
    driver = HlaDriver('0', port=port)
    driver._hla.decode(slot_frame(0, 0, frame_num=0, start_time=0.0))
    driver._hla.decode(slot_frame(0, 0, frame_num=1, start_time=1e-9))
    assert driver._hla._sample_rate == 48000
    driver.shutdown()


def test_sample_rate_at_large_timestamp(port):
    """Sample rate derivation works at large timestamps (e.g. 86400 seconds)."""
    driver = HlaDriver('0', port=port)
    t0 = 86400.0
    t1 = t0 + 1.0 / 48000
    driver._hla.decode(slot_frame(0, 0, frame_num=0, start_time=t0))
    driver._hla.decode(slot_frame(0, 0, frame_num=1, start_time=t1))
    assert driver._hla._sample_rate == 48000
    driver.shutdown()


# ===========================================================================
# 13. WAV export additional coverage
# ===========================================================================

def test_wav_init_error_emitted_once(tmp_path):
    """TdmWavExport with empty output_path should emit error frame once, then None."""
    import TdmWavExport as _wav_mod
    from TdmAudioStream import AnalyzerFrame as _AF
    # Patch the AnalyzerFrame stub to one that accepts constructor args
    _orig_af = _wav_mod.AnalyzerFrame
    _wav_mod.AnalyzerFrame = _AF
    try:
        hla = _wav_mod.TdmWavExport.__new__(_wav_mod.TdmWavExport)
        hla.slots = '0,1'
        hla.output_path = ''  # empty string -> init error
        hla.bit_depth = '16'
        hla.__init__()

        assert hla._init_error is not None

        frame = FakeFrame('slot', 0.0, 0.001, {'slot': 0, 'data': 0, 'frame_number': 0})
        result = hla.decode(frame)
        assert result is not None
        assert result.type == 'error'

        result2 = hla.decode(frame)
        assert result2 is None
    finally:
        _wav_mod.AnalyzerFrame = _orig_af


def test_wav_advisory_frame_ignored(tmp_path):
    """TdmWavExport should ignore advisory frames."""
    path = str(tmp_path / 'advisory_test.wav')
    hla = make_wav_hla(path, '0,1', '16')
    frame = FakeFrame('advisory', 0.0, 0.001, {'severity': 'warning', 'message': 'test'})
    result = hla.decode(frame)
    assert result is None
    hla.shutdown()


def test_wav_shutdown_no_data(tmp_path):
    """TdmWavExport shutdown with no data fed should not crash."""
    path = str(tmp_path / 'empty.wav')
    hla = make_wav_hla(path, '0', '16')
    # Don't feed any frames -- just shutdown
    hla.shutdown()
    # WAV file should either not exist or have no audio frames
    if os.path.exists(path):
        with wave.open(path, 'rb') as wf:
            assert wf.getnframes() == 0


# ===========================================================================
# 14. parse_slot_spec edge cases
# ===========================================================================

def test_parse_slot_spec_empty_raises():
    """parse_slot_spec('') should raise ValueError."""
    from _tdm_utils import parse_slot_spec
    with pytest.raises(ValueError):
        parse_slot_spec("")


def test_parse_slot_spec_trailing_comma():
    """parse_slot_spec('0,1,') should return [0, 1] (trailing empty token ignored)."""
    from _tdm_utils import parse_slot_spec
    result = parse_slot_spec("0,1,")
    assert result == [0, 1]


def test_parse_slot_spec_reversed_range_raises():
    """parse_slot_spec('5-2') should raise ValueError."""
    from _tdm_utils import parse_slot_spec
    with pytest.raises(ValueError):
        parse_slot_spec("5-2")


def test_parse_slot_spec_overlapping_ranges_deduped():
    """parse_slot_spec('0-3,2-5') should return [0, 1, 2, 3, 4, 5] (deduplicated)."""
    from _tdm_utils import parse_slot_spec
    result = parse_slot_spec("0-3,2-5")
    assert result == [0, 1, 2, 3, 4, 5]


def test_parse_slot_spec_negative_number():
    """parse_slot_spec('-1') should raise ValueError."""
    from _tdm_utils import parse_slot_spec
    with pytest.raises(ValueError):
        parse_slot_spec("-1")


# ===========================================================================
# 15. Empty batch flush guard
# ===========================================================================

def test_flush_batch_empty_is_noop(port):
    """Calling _flush_batch() when _batch_count is 0 should not change the ring."""
    driver = HlaDriver('0', port=port, buffer_size=128)
    warmup(driver)
    driver._hla._flush_batch()  # flush any warmup residual
    initial_ring_len = len(driver._hla._ring)
    # Call _flush_batch() again with empty batch
    assert driver._hla._batch_count == 0
    driver._hla._flush_batch()
    assert len(driver._hla._ring) == initial_ring_len
    driver.shutdown()


# ===========================================================================
# 16. Audio Batch Mode (LLA sends "audio_batch" frames to HLA)
# ===========================================================================

def _make_audio_batch_frame(samples, channels, bit_depth, sample_rate,
                             start_frame_num=0, start_time=0.0):
    """Build a FakeFrame of type 'audio_batch' with packed PCM.

    Args:
        samples: list of lists, e.g. [[ch0, ch1], [ch0, ch1], ...]
        channels: number of channels in the LLA output
        bit_depth: bits per sample
        sample_rate: audio sample rate
    """
    num_frames = len(samples)
    bytes_per_sample = (bit_depth + 7) // 8
    fmt_char = {1: 'b', 2: '<h', 4: '<i'}[bytes_per_sample]

    pcm = bytearray()
    for frame_samples in samples:
        for ch in range(channels):
            val = frame_samples[ch] if ch < len(frame_samples) else 0
            pcm.extend(struct.pack(fmt_char, val))

    end_time = start_time + num_frames / sample_rate
    return FakeFrame('audio_batch', start_time, end_time, {
        'pcm_data': bytes(pcm),
        'num_frames': num_frames,
        'channels': channels,
        'bit_depth': bit_depth,
        'sample_rate': sample_rate,
        'start_frame_number': start_frame_num,
    })


def _batch_stream_test(driver, port, samples, channels, bit_depth, sample_rate,
                        slot_filter_channels=None):
    """Helper: connect TCP client, feed batch frames, read decoded PCM.

    Connects the client first so the handshake can be sent during decode.
    Returns (handshake_dict, decoded_frames_list).
    """
    # Connect client first
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect(('127.0.0.1', port))
    time.sleep(0.1)  # Let accept_loop register the client

    # Feed batch frame(s)
    batch = _make_audio_batch_frame(samples, channels, bit_depth, sample_rate)
    driver.feed([batch])

    time.sleep(0.3)  # Let sender thread transmit

    # Read handshake + PCM
    sock.settimeout(2.0)
    buf = b''
    while b'\n' not in buf:
        try:
            buf += sock.recv(4096)
        except socket.timeout:
            break
    if b'\n' not in buf:
        sock.close()
        return None, []

    line, remainder = buf.split(b'\n', 1)
    handshake = json.loads(line)

    out_ch = slot_filter_channels or handshake['channels']
    decoded = _read_frames_from_sock(sock, out_ch, bit_depth, remainder, timeout=1.0)
    sock.close()
    return handshake, decoded


def test_audio_batch_basic_stream(port):
    """Audio batch frames are forwarded to the TCP ring buffer."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)

    samples = [[100, 200], [300, 400], [500, 600], [700, 800]]
    handshake, decoded = _batch_stream_test(driver, port, samples, 2, 16, 48000)

    assert handshake is not None, "Should have received handshake"
    assert handshake['sample_rate'] == 48000
    assert handshake['channels'] == 2
    assert handshake['bit_depth'] == 16

    assert len(decoded) >= 4, f"Expected 4 frames, got {len(decoded)}"
    assert decoded[0] == [100, 200]
    assert decoded[1] == [300, 400]
    assert decoded[2] == [500, 600]
    assert decoded[3] == [700, 800]

    driver.shutdown()


def test_audio_batch_sample_rate_from_metadata(port):
    """Sample rate is derived from batch metadata, not timing."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)

    # No warmup - the batch frame provides sample_rate directly
    samples = [[1, 2]]
    batch = _make_audio_batch_frame(samples, 2, 16, 96000)
    driver.feed([batch])

    assert driver._hla._sample_rate == 96000
    driver.shutdown()


def test_audio_batch_slot_filter_subset(port):
    """HLA configured for slots 0,2 extracts only those from a 4-channel batch."""
    driver = HlaDriver('0,2', port=port, buffer_size=128)

    samples = [
        [10, 20, 30, 40],
        [50, 60, 70, 80],
    ]
    handshake, decoded = _batch_stream_test(
        driver, port, samples, 4, 16, 48000, slot_filter_channels=2)

    assert handshake['channels'] == 2
    assert len(decoded) >= 2
    assert decoded[0] == [10, 30], f"Expected [10, 30], got {decoded[0]}"
    assert decoded[1] == [50, 70], f"Expected [50, 70], got {decoded[1]}"

    driver.shutdown()


def test_audio_batch_slot_filter_all_channels(port):
    """When HLA requests all channels in order, fast path is used (no repack)."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)

    samples = [[111, 222], [333, 444]]
    handshake, decoded = _batch_stream_test(driver, port, samples, 2, 16, 48000)

    assert len(decoded) >= 2
    assert decoded[0] == [111, 222]
    assert decoded[1] == [333, 444]
    driver.shutdown()


def test_audio_batch_32bit(port):
    """32-bit audio batch frames are handled correctly."""
    driver = HlaDriver('0,1', port=port, buffer_size=128, bit_depth=32)

    samples = [[100000, -200000], [300000, -400000]]
    handshake, decoded = _batch_stream_test(driver, port, samples, 2, 32, 48000)

    assert handshake['bit_depth'] == 32
    assert len(decoded) >= 2
    assert decoded[0] == [100000, -200000]
    assert decoded[1] == [300000, -400000]
    driver.shutdown()


def test_audio_batch_multiple_batches(port):
    """Multiple consecutive audio_batch frames accumulate correctly."""
    driver = HlaDriver('0,1', port=port, buffer_size=256)

    # Connect first
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect(('127.0.0.1', port))
    time.sleep(0.1)

    batch1 = _make_audio_batch_frame(
        [[1, 2], [3, 4]], 2, 16, 48000, start_frame_num=0)
    batch2 = _make_audio_batch_frame(
        [[5, 6], [7, 8]], 2, 16, 48000, start_frame_num=2,
        start_time=2/48000)
    batch3 = _make_audio_batch_frame(
        [[9, 10], [11, 12]], 2, 16, 48000, start_frame_num=4,
        start_time=4/48000)

    driver.feed([batch1, batch2, batch3])
    time.sleep(0.3)

    # Read handshake + all PCM
    sock.settimeout(2.0)
    buf = b''
    while b'\n' not in buf:
        buf += sock.recv(4096)
    line, remainder = buf.split(b'\n', 1)
    handshake = json.loads(line)

    decoded = _read_frames_from_sock(sock, 2, 16, remainder, timeout=1.0)
    sock.close()

    assert len(decoded) >= 6, f"Expected 6 frames, got {len(decoded)}"
    assert decoded[0] == [1, 2]
    assert decoded[1] == [3, 4]
    assert decoded[2] == [5, 6]
    assert decoded[3] == [7, 8]
    assert decoded[4] == [9, 10]
    assert decoded[5] == [11, 12]
    driver.shutdown()


def test_audio_batch_empty_pcm_ignored(port):
    """Batch frame with empty pcm_data is silently ignored."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)

    empty_batch = FakeFrame('audio_batch', 0.0, 0.001, {
        'pcm_data': b'',
        'num_frames': 0,
        'channels': 2,
        'bit_depth': 16,
        'sample_rate': 48000,
        'start_frame_number': 0,
    })
    result = driver._hla.decode(empty_batch)
    assert result is None
    assert driver._hla._frame_count == 0
    driver.shutdown()


def test_audio_batch_missing_fields_handled(port):
    """Batch frame with missing optional fields doesn't crash."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)

    # Minimal batch frame - some fields missing
    minimal_batch = FakeFrame('audio_batch', 0.0, 0.001, {
        'pcm_data': struct.pack('<2h', 42, 43),
        'num_frames': 1,
        'channels': 2,
        'bit_depth': 16,
        'sample_rate': 48000,
    })
    result = driver._hla.decode(minimal_batch)
    assert result is None
    driver.shutdown()


def test_audio_batch_mixed_with_slot_frames(port):
    """Slot frames followed by batch frames both work in the same session."""
    driver = HlaDriver('0,1', port=port, buffer_size=256)

    # First: send slot frames to derive sample rate and stream some data
    warmup(driver, sample_rate=48000, n=5)
    driver._hla._flush_batch()

    initial_count = driver._hla._frame_count

    # Then: send a batch frame
    samples = [[1000, 2000], [3000, 4000]]
    batch = _make_audio_batch_frame(samples, 2, 16, 48000)
    driver.feed([batch])

    # frame_count should increase by num_frames in batch
    assert driver._hla._frame_count == initial_count + 2

    driver.shutdown()


def test_audio_batch_no_sample_rate_waits(port):
    """Batch frame without sample_rate field doesn't send data."""
    driver = HlaDriver('0,1', port=port, buffer_size=128)

    # Batch with sample_rate=0 means unknown
    batch = FakeFrame('audio_batch', 0.0, 0.001, {
        'pcm_data': struct.pack('<2h', 1, 2),
        'num_frames': 1,
        'channels': 2,
        'bit_depth': 16,
        'sample_rate': 0,
        'start_frame_number': 0,
    })
    result = driver._hla.decode(batch)
    assert result is None
    # Sample rate should still be None
    assert driver._hla._sample_rate is None
    driver.shutdown()
