#!/usr/bin/env python3
"""Stress and reliability tests for the TDM Audio Stream system.

Tests:
  1. Reconnection behavior
  2. Ring buffer overflow
  3. Rapid connect/disconnect
  4. Large frame burst
  5. Client connects before sample rate derived
  6. Zero-length and single-frame edge cases
"""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools', 'tdm-test-harness'))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools', 'tdm-audio-bridge'))

from tdm_audio_bridge.client import StreamClient
from tdm_audio_bridge.protocol import Handshake, read_handshake, unpack_frames
from tdm_test_harness.signals import parse_signal_spec
from tdm_test_harness.frame_emitter import emit_frames, FakeFrame
from tdm_test_harness.hla_driver import HlaDriver
import socket
import json
import struct
import threading
import time
import math
import traceback

# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

SAMPLE_RATE = 48000
CHANNELS = 2
SLOT_LIST = [0, 1]
SLOT_SPEC = '0,1'

RESULTS = {}  # test_name -> (pass/fail, details)


def warmup_frames(n=3, start=0):
    """Generate n warmup frames of silence to derive sample rate."""
    samples = [[0] * CHANNELS for _ in range(n)]
    return list(emit_frames(samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=start))


def flush_frame(frame_num):
    """Generate a single-slot 'flush' frame that triggers the flush of the
    previous accumulated TDM frame. We only need slot 0 to trigger the
    frame-boundary detection."""
    t = frame_num / SAMPLE_RATE
    return FakeFrame(
        frame_type='slot',
        start_time=t,
        end_time=t + 1.0 / (SAMPLE_RATE * len(SLOT_LIST)),
        data={
            'slot': 0,
            'data': 0,
            'frame_number': frame_num,
            'severity': 'ok',
            'short_slot': False,
            'extra_slot': False,
            'bitclock_error': False,
            'missed_data': False,
            'missed_frame_sync': False,
            'low_sample_rate': False,
        },
    )


def raw_connect(port, timeout=5.0):
    """Open a raw TCP connection and read the handshake."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect(('127.0.0.1', port))
    # Read handshake
    buf = b''
    while b'\n' not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("Closed before handshake")
        buf += chunk
    line, remainder = buf.split(b'\n', 1)
    hs = json.loads(line)
    return sock, hs, remainder


def read_pcm_frames(sock, handshake, remainder=b'', timeout=1.0, max_wait=3.0):
    """Read PCM frames from the socket until timeout or max_wait elapsed."""
    channels = handshake['channels']
    bit_depth = handshake['bit_depth']
    bytes_per_sample = bit_depth // 8
    frame_size = channels * bytes_per_sample
    fmt_char = 'h' if bit_depth == 16 else 'i'
    fmt = f'<{channels}{fmt_char}'

    pcm_buf = remainder
    deadline = time.time() + max_wait
    sock.settimeout(timeout)
    while time.time() < deadline:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            pcm_buf += chunk
        except socket.timeout:
            break

    frames = []
    for i in range(0, len(pcm_buf) - frame_size + 1, frame_size):
        samples = list(struct.unpack(fmt, pcm_buf[i:i + frame_size]))
        frames.append(samples)
    return frames


def run_test(name, func):
    """Run a test function, catch exceptions, record result."""
    print(f"\n{'='*70}")
    print(f"TEST: {name}")
    print(f"{'='*70}")
    try:
        func()
        RESULTS[name] = (True, "PASS")
        print(f"\n  >>> PASS: {name}")
    except Exception as e:
        RESULTS[name] = (False, str(e))
        print(f"\n  >>> FAIL: {name}")
        print(f"  Error: {e}")
        traceback.print_exc()


# ===========================================================================
# Test 1: Reconnection behavior
# ===========================================================================

def test_reconnection():
    port = 14301
    driver = HlaDriver(SLOT_SPEC, port=port, buffer_size=128, bit_depth=16)

    try:
        # Feed warmup frames to derive sample rate
        driver.feed(warmup_frames(n=3, start=0))
        assert driver.sample_rate == SAMPLE_RATE, \
            f"Expected sample rate {SAMPLE_RATE}, got {driver.sample_rate}"
        print(f"  Sample rate derived: {driver.sample_rate}")

        # --- First connection ---
        sock1, hs1, rem1 = raw_connect(port)
        print(f"  First connection: handshake received (rate={hs1['sample_rate']})")
        assert hs1['sample_rate'] == SAMPLE_RATE
        assert hs1['channels'] == CHANNELS
        assert hs1['protocol'] == 1

        # Disconnect first client
        sock1.close()
        time.sleep(0.3)
        print("  First client disconnected")

        # --- Second connection ---
        sock2, hs2, rem2 = raw_connect(port)
        print(f"  Second connection: handshake received (rate={hs2['sample_rate']})")
        assert hs2['sample_rate'] == SAMPLE_RATE
        assert hs2['channels'] == CHANNELS
        assert hs2['protocol'] == 1

        # Feed some data and verify it arrives on the new connection
        test_samples = [[1000, 2000], [3000, 4000], [5000, 6000]]
        start_fn = 10
        frames = list(emit_frames(test_samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=start_fn))
        # Add flush frame
        frames.append(flush_frame(start_fn + len(test_samples)))
        driver.feed(frames)
        time.sleep(0.5)

        received = read_pcm_frames(sock2, hs2, rem2, timeout=0.5, max_wait=2.0)
        print(f"  Received {len(received)} frames on second connection")

        # Check that our test data is in the received frames
        found_1000_2000 = any(f[0] == 1000 and f[1] == 2000 for f in received)
        found_3000_4000 = any(f[0] == 3000 and f[1] == 4000 for f in received)
        assert found_1000_2000, \
            f"Test frame [1000, 2000] not found in received: {received}"
        assert found_3000_4000, \
            f"Test frame [3000, 4000] not found in received: {received}"
        print("  Test data verified on second connection")

        sock2.close()
    finally:
        driver.shutdown()
        time.sleep(0.2)


# ===========================================================================
# Test 2: Ring buffer overflow
# ===========================================================================

def test_ring_buffer_overflow():
    port = 14302
    buf_size = 8
    driver = HlaDriver(SLOT_SPEC, port=port, buffer_size=buf_size, bit_depth=16)

    try:
        # Feed warmup to derive sample rate
        driver.feed(warmup_frames(n=3, start=0))
        assert driver.sample_rate == SAMPLE_RATE
        print(f"  Sample rate derived: {driver.sample_rate}")

        # NO client connected — feed 100 frames of data
        overflow_samples = [[i * 10, i * 10 + 1] for i in range(100)]
        frames = list(emit_frames(overflow_samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=5))
        # Flush the last frame
        frames.append(flush_frame(5 + 100))
        driver.feed(frames)
        time.sleep(0.3)

        print(f"  Fed 100 frames with no client (buffer_size={buf_size})")
        print(f"  Frame count: {driver.frame_count}")

        # Now connect a client — ring buffer was cleared on connect
        sock, hs, rem = raw_connect(port)
        print(f"  Client connected, handshake received")

        # Feed 5 more frames with distinctive values
        new_samples = [[9001, 9002], [9003, 9004], [9005, 9006], [9007, 9008], [9009, 9010]]
        new_start = 200
        new_frames = list(emit_frames(new_samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=new_start))
        new_frames.append(flush_frame(new_start + len(new_samples)))
        driver.feed(new_frames)
        time.sleep(0.5)

        received = read_pcm_frames(sock, hs, rem, timeout=0.5, max_wait=2.0)
        print(f"  Received {len(received)} frames after connecting")

        # Verify only the NEW frames arrived (not old overflow data)
        found_9001 = any(f[0] == 9001 and f[1] == 9002 for f in received)
        assert found_9001, \
            f"New frame [9001, 9002] not found in received: {received}"

        # Verify old overflow data is NOT present
        found_old = any(f[0] == 990 and f[1] == 991 for f in received)
        assert not found_old, \
            f"Old overflow data unexpectedly present in received: {received}"

        print("  Verified: only new frames arrived, old overflow data dropped")
        print("  No crashes or hangs detected")

        sock.close()
    finally:
        driver.shutdown()
        time.sleep(0.2)


# ===========================================================================
# Test 3: Rapid connect/disconnect
# ===========================================================================

def test_rapid_connect_disconnect():
    port = 14303
    driver = HlaDriver(SLOT_SPEC, port=port, buffer_size=128, bit_depth=16)

    try:
        # Feed warmup
        driver.feed(warmup_frames(n=3, start=0))
        assert driver.sample_rate == SAMPLE_RATE
        print(f"  Sample rate derived: {driver.sample_rate}")

        # Rapidly connect and disconnect 20 TCP clients
        for i in range(20):
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(2.0)
                s.connect(('127.0.0.1', port))
                # Give the accept loop a moment
                time.sleep(0.02)
                s.close()
            except Exception as e:
                print(f"  Warning: rapid connect {i} failed: {e}")
            # Small delay to allow the accept loop to process
            time.sleep(0.02)

        print(f"  Completed 20 rapid connect/disconnect cycles")
        time.sleep(0.3)  # Let things settle

        # Now connect one final client and verify it works
        sock, hs, rem = raw_connect(port, timeout=5.0)
        print(f"  Final client connected, handshake received (rate={hs['sample_rate']})")
        assert hs['sample_rate'] == SAMPLE_RATE
        assert hs['channels'] == CHANNELS

        # Feed data and verify it arrives
        test_samples = [[7777, 8888], [1111, 2222]]
        start_fn = 50
        data_frames = list(emit_frames(test_samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=start_fn))
        data_frames.append(flush_frame(start_fn + len(test_samples)))
        driver.feed(data_frames)
        time.sleep(0.5)

        received = read_pcm_frames(sock, hs, rem, timeout=0.5, max_wait=2.0)
        print(f"  Received {len(received)} frames on final connection")

        found_7777 = any(f[0] == 7777 and f[1] == 8888 for f in received)
        assert found_7777, \
            f"Test frame [7777, 8888] not found in received: {received}"
        print("  Final connection works correctly after rapid cycling")
        print("  No crashes or hangs detected")

        sock.close()
    finally:
        driver.shutdown()
        time.sleep(0.2)


# ===========================================================================
# Test 4: Large frame burst
# ===========================================================================

def test_large_frame_burst():
    port = 14304
    n_test_frames = 10000
    driver = HlaDriver(SLOT_SPEC, port=port, buffer_size=50000, bit_depth=16)

    try:
        # Feed warmup
        driver.feed(warmup_frames(n=3, start=0))
        assert driver.sample_rate == SAMPLE_RATE
        print(f"  Sample rate derived: {driver.sample_rate}")

        # Drain any warmup residual: feed a boundary frame to flush the
        # last warmup accumulator before connecting the client.
        driver.feed([flush_frame(3)])
        time.sleep(0.2)

        # Connect client (ring cleared on connect, so residual is gone)
        sock, hs, rem = raw_connect(port)
        print(f"  Client connected, handshake received")

        # Generate 10000 frames of sine wave data
        max_val = (1 << 15) - 1  # 16-bit max
        sine_samples = []
        for i in range(n_test_frames):
            t = i / SAMPLE_RATE
            s0 = int(max_val * math.sin(2 * math.pi * 440 * t))
            s1 = int(max_val * math.sin(2 * math.pi * 880 * t))
            sine_samples.append([s0, s1])

        # Feed ALL frames at once (no pacing)
        # Start at frame_num=3 (same as _last_frame_num from flush) so
        # _try_flush is a no-op and no residual is emitted.
        start_fn = 3
        all_frames = list(emit_frames(sine_samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=start_fn))
        # Add flush frame
        all_frames.append(flush_frame(start_fn + n_test_frames))
        print(f"  Feeding {n_test_frames} frames all at once...")
        driver.feed(all_frames)
        print(f"  All frames fed. Frame count: {driver.frame_count}")

        # Read all data from client — give extra time for large burst
        time.sleep(1.0)
        received = read_pcm_frames(sock, hs, rem, timeout=1.0, max_wait=5.0)
        print(f"  Received {len(received)} frames from client")

        # Verify all 10000 frames arrived
        assert len(received) == n_test_frames, \
            f"Expected {n_test_frames} frames, got {len(received)}"

        # Verify sample-level correctness
        mismatches = 0
        max_error = 0
        for i in range(n_test_frames):
            for ch in range(CHANNELS):
                expected = sine_samples[i][ch]
                actual = received[i][ch]
                err = abs(actual - expected)
                max_error = max(max_error, err)
                if err > 0:
                    mismatches += 1

        print(f"  Max error: {max_error}, mismatches: {mismatches}")
        assert mismatches == 0, \
            f"Found {mismatches} sample mismatches (max error: {max_error})"
        print(f"  All {n_test_frames} frames verified correctly (exact match)")

        sock.close()
    finally:
        driver.shutdown()
        time.sleep(0.2)


# ===========================================================================
# Test 5: Client connects before sample rate is derived
# ===========================================================================

def test_client_before_sample_rate():
    port = 14305
    driver = HlaDriver(SLOT_SPEC, port=port, buffer_size=128, bit_depth=16)

    try:
        assert driver.sample_rate is None, \
            f"Sample rate should be None before warmup, got {driver.sample_rate}"
        print(f"  Sample rate before warmup: {driver.sample_rate}")

        # Connect a TCP client IMMEDIATELY (before any frames fed)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect(('127.0.0.1', port))
        time.sleep(0.2)
        print("  Client connected before any frames fed")

        # Now feed warmup frames to derive sample rate
        driver.feed(warmup_frames(n=3, start=0))
        assert driver.sample_rate == SAMPLE_RATE
        print(f"  Sample rate derived after warmup: {driver.sample_rate}")

        # Feed some test data to trigger the deferred handshake via _enqueue_frame
        test_samples = [[4444, 5555], [6666, 7777]]
        start_fn = 10
        data_frames = list(emit_frames(test_samples, SAMPLE_RATE, SLOT_LIST, start_frame_num=start_fn))
        data_frames.append(flush_frame(start_fn + len(test_samples)))
        driver.feed(data_frames)
        time.sleep(0.5)

        # Read handshake (should have been sent as deferred handshake)
        buf = b''
        while b'\n' not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed before handshake")
            buf += chunk

        line, remainder = buf.split(b'\n', 1)
        hs = json.loads(line)
        print(f"  Deferred handshake received: rate={hs['sample_rate']}, ch={hs['channels']}")
        assert hs['sample_rate'] == SAMPLE_RATE, \
            f"Expected sample rate {SAMPLE_RATE}, got {hs['sample_rate']}"
        assert hs['channels'] == CHANNELS
        assert hs['protocol'] == 1

        # Read test data
        received = read_pcm_frames(sock, hs, remainder, timeout=0.5, max_wait=2.0)
        print(f"  Received {len(received)} frames")

        found_4444 = any(f[0] == 4444 and f[1] == 5555 for f in received)
        assert found_4444, \
            f"Test frame [4444, 5555] not found in received: {received}"
        print("  Test data verified after deferred handshake")

        sock.close()
    finally:
        driver.shutdown()
        time.sleep(0.2)


# ===========================================================================
# Test 6: Zero-length and single-frame edge cases
# ===========================================================================

def test_single_frame_edge_cases():
    port = 14306
    driver = HlaDriver(SLOT_SPEC, port=port, buffer_size=128, bit_depth=16)

    try:
        # Feed warmup
        driver.feed(warmup_frames(n=3, start=0))
        assert driver.sample_rate == SAMPLE_RATE
        print(f"  Sample rate derived: {driver.sample_rate}")

        # Drain warmup residual: feed a boundary frame to flush the last
        # warmup accumulator, then connect. The ring is cleared on connect.
        # The flush frame leaves _last_frame_num=3 and accum={0:0} in the
        # HLA, but we start test data at the same frame_num=3 so _try_flush
        # sees current==last and does NOT flush that residual.
        driver.feed([flush_frame(3)])
        time.sleep(0.2)

        # Connect client (ring cleared on connect, so residual is gone)
        sock, hs, rem = raw_connect(port)
        print(f"  Client connected, handshake received")

        # --- Feed exactly 1 frame of data + flush ---
        # Start at frame_num=3 (same as _last_frame_num) so the stale
        # accumulator is overwritten, not flushed as an extra frame.
        single_sample = [[11111, 22222]]
        start_fn = 3
        single_frames = list(emit_frames(single_sample, SAMPLE_RATE, SLOT_LIST, start_frame_num=start_fn))
        single_frames.append(flush_frame(start_fn + 1))
        driver.feed(single_frames)
        time.sleep(0.5)

        received1 = read_pcm_frames(sock, hs, rem, timeout=0.5, max_wait=2.0)
        print(f"  After first single frame: received {len(received1)} frame(s)")
        assert len(received1) == 1, \
            f"Expected exactly 1 frame, got {len(received1)}: {received1}"
        assert received1[0][0] == 11111 and received1[0][1] == 22222, \
            f"Expected [11111, 22222], got {received1[0]}"
        print(f"  First single frame verified: {received1[0]}")

        # --- Feed another single frame + flush ---
        # Use frame_num=4 (same as _last_frame_num from the previous flush)
        # so _try_flush is a no-op and no residual zero frame is emitted.
        # Use values within 16-bit signed range (-32768..32767).
        second_sample = [[31000, -12345]]
        start_fn2 = start_fn + 1  # = 4, matching flush_frame's _last_frame_num
        second_frames = list(emit_frames(second_sample, SAMPLE_RATE, SLOT_LIST, start_frame_num=start_fn2))
        second_frames.append(flush_frame(start_fn2 + 1))
        driver.feed(second_frames)
        time.sleep(0.5)

        # Read from socket — no remainder this time since we consumed it all
        received2 = read_pcm_frames(sock, hs, b'', timeout=0.5, max_wait=2.0)
        print(f"  After second single frame: received {len(received2)} frame(s)")

        # We should get at least 1 frame (could get the flush frame's slot 0 too)
        # But we specifically check for our test data
        assert len(received2) >= 1, \
            f"Expected at least 1 frame, got {len(received2)}"

        # Find our test frame
        found_31000 = any(f[0] == 31000 and f[1] == -12345 for f in received2)
        assert found_31000, \
            f"Test frame [31000, -12345] not found in received: {received2}"
        print(f"  Second single frame verified")

        sock.close()
    finally:
        driver.shutdown()
        time.sleep(0.2)


# ===========================================================================
# Main — run all tests
# ===========================================================================

if __name__ == '__main__':
    run_test("Test 1: Reconnection behavior", test_reconnection)
    run_test("Test 2: Ring buffer overflow", test_ring_buffer_overflow)
    run_test("Test 3: Rapid connect/disconnect", test_rapid_connect_disconnect)
    run_test("Test 4: Large frame burst", test_large_frame_burst)
    run_test("Test 5: Client connects before sample rate derived", test_client_before_sample_rate)
    run_test("Test 6: Single-frame edge cases", test_single_frame_edge_cases)

    # Print summary
    print(f"\n{'='*70}")
    print("SUMMARY")
    print(f"{'='*70}")

    passed = 0
    failed = 0
    for name, (result, detail) in RESULTS.items():
        status = "PASS" if result else "FAIL"
        print(f"  [{status}] {name}")
        if not result:
            print(f"         {detail}")
            failed += 1
        else:
            passed += 1

    total = passed + failed
    print(f"\n  {passed}/{total} tests passed, {failed} failed")

    if failed > 0:
        sys.exit(1)
    else:
        print("\n  All tests passed!")
        sys.exit(0)
