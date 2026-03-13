"""TCP protocol handling for the TDM Audio Stream.

Matches the HLA's handshake format:
  - JSON line terminated by \\n (newline-delimited)
  - Fields: protocol, sample_rate, channels, bit_depth, slot_list,
            buffer_size, byte_order

After handshake, raw interleaved little-endian PCM follows.
"""

import json
import socket
import struct
import logging

log = logging.getLogger('tdm-audio-bridge')

PROTOCOL_VERSION = 1


class Handshake:
    """Parsed handshake from the HLA TCP server."""

    __slots__ = ('protocol', 'sample_rate', 'channels', 'bit_depth',
                 'slot_list', 'buffer_size', 'byte_order',
                 'frame_size', 'struct_fmt')

    def __init__(self, data: dict):
        self.protocol = data['protocol']
        self.sample_rate = data['sample_rate']
        self.channels = data['channels']
        self.bit_depth = data['bit_depth']
        self.slot_list = data['slot_list']
        self.buffer_size = data.get('buffer_size', 128)
        self.byte_order = data.get('byte_order', 'little')

        if self.protocol != PROTOCOL_VERSION:
            raise ValueError(
                f"Unsupported protocol version {self.protocol} "
                f"(expected {PROTOCOL_VERSION})"
            )

        bytes_per_sample = self.bit_depth // 8
        self.frame_size = self.channels * bytes_per_sample
        fmt_char = 'h' if self.bit_depth == 16 else 'i'
        self.struct_fmt = f'<{self.channels}{fmt_char}'

    def __repr__(self):
        return (f"Handshake(rate={self.sample_rate}, ch={self.channels}, "
                f"depth={self.bit_depth}, slots={self.slot_list})")


def read_handshake(sock: socket.socket) -> Handshake:
    """Read the JSON handshake line from the HLA TCP server.

    Returns a Handshake object with parsed metadata.
    Raises ConnectionError if the connection drops before handshake completes.
    """
    buf = b''
    while b'\n' not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("Connection closed before handshake completed")
        buf += chunk

    line, _remainder = buf.split(b'\n', 1)
    data = json.loads(line)
    hs = Handshake(data)
    log.info("Handshake: %s", hs)
    return hs, _remainder


def unpack_frames(buf: bytes, handshake: Handshake):
    """Unpack raw PCM bytes into a list of sample tuples.

    Returns (frames, remainder) where remainder is leftover bytes
    that don't form a complete frame.
    """
    fs = handshake.frame_size
    n_complete = len(buf) // fs
    frames = []
    for i in range(n_complete):
        offset = i * fs
        samples = struct.unpack(handshake.struct_fmt, buf[offset:offset + fs])
        frames.append(samples)
    remainder = buf[n_complete * fs:]
    return frames, remainder
