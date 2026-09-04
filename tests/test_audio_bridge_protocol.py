"""Unit tests for the tdm-audio-bridge wire protocol parser.

The bridge speaks int16 or int32 PCM only. Before v2.6.0 a handshake
advertising bit_depth=24 (which a 24-bit LLA in Audio Batch Mode could
produce) computed a 3-byte frame size but a 4-byte struct format, and
unpack_frames() crashed with struct.error on the first frame. The stream
HLA now widens 24-bit to int32 before sending, and the bridge rejects any
unsupported width with an actionable error instead of a traceback.
"""
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools', 'tdm-audio-bridge'))

from tdm_audio_bridge.protocol import Handshake, unpack_frames, PROTOCOL_VERSION


def _hs(bit_depth, channels=2):
    return {
        'protocol': PROTOCOL_VERSION,
        'sample_rate': 48000,
        'channels': channels,
        'bit_depth': bit_depth,
        'slot_list': list(range(channels)),
        'buffer_size': 128,
        'byte_order': 'little',
    }


def test_handshake_accepts_16_bit():
    hs = Handshake(_hs(16))
    assert hs.frame_size == 4
    assert hs.struct_fmt == '<2h'


def test_handshake_accepts_32_bit():
    hs = Handshake(_hs(32))
    assert hs.frame_size == 8
    assert hs.struct_fmt == '<2i'


@pytest.mark.parametrize('depth', [8, 24, 12, 64])
def test_handshake_rejects_unsupported_bit_depth(depth):
    with pytest.raises(ValueError) as ei:
        Handshake(_hs(depth))
    msg = str(ei.value)
    assert str(depth) in msg
    assert '16' in msg and '32' in msg


def test_unpack_frames_16_bit_roundtrip():
    hs = Handshake(_hs(16))
    buf = struct.pack('<4h', 1, -1, 32767, -32768) + b'\x01'  # trailing partial
    frames, rem = unpack_frames(buf, hs)
    assert frames == [(1, -1), (32767, -32768)]
    assert rem == b'\x01'


def test_unpack_frames_32_bit_roundtrip():
    hs = Handshake(_hs(32, channels=1))
    buf = struct.pack('<3i', 0x01234500, -256, -0x80000000)
    frames, rem = unpack_frames(buf, hs)
    assert frames == [(0x01234500,), (-256,), (-0x80000000,)]
    assert rem == b''
