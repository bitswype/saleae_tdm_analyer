"""TCP client that connects to the HLA's TCP server, receives the handshake
and PCM stream, and compares against expected sample data.

Designed for automated testing — returns structured results with pass/fail
status, suitable for agent-driven iteration.
"""

import socket
import json
import struct


class Verifier:
    """Connects to the TDM Audio Stream HLA and verifies PCM output."""

    def __init__(self, host='127.0.0.1', port=4011, timeout=10.0):
        self._host = host
        self._port = port
        self._timeout = timeout

    def verify(self, expected_frames, tolerance=1, skip_first=0):
        """Connect, receive PCM, compare against expected samples.

        Args:
            expected_frames: List of lists of expected sample values (one
                             list per frame, one value per channel).
            tolerance: Maximum allowed absolute error per sample.
            skip_first: Number of initial received frames to skip before
                        comparing (for warmup residual).

        Returns:
            Dict with structured results:
            {
                "pass": bool,
                "handshake": dict,
                "frames_expected": int,
                "frames_received": int,
                "frames_compared": int,
                "max_error": int,
                "mismatches": int,
                "sample_rate_derived": int,
                "error": str or None,
            }
        """
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self._timeout)

        try:
            sock.connect((self._host, self._port))
        except (ConnectionRefusedError, OSError) as e:
            return {
                'pass': False,
                'error': f'Connection failed: {e}',
                'handshake': None,
                'frames_expected': len(expected_frames),
                'frames_received': 0,
                'frames_compared': 0,
                'max_error': 0,
                'mismatches': 0,
                'sample_rate_derived': None,
            }

        try:
            # Read handshake (JSON line terminated by \n)
            buf = b''
            while b'\n' not in buf:
                chunk = sock.recv(4096)
                if not chunk:
                    return self._error_result(
                        'Connection closed before handshake',
                        expected_frames,
                    )
                buf += chunk

            line, remainder = buf.split(b'\n', 1)
            handshake = json.loads(line)

            channels = handshake['channels']
            bit_depth = handshake['bit_depth']
            sample_rate = handshake['sample_rate']

            # Determine frame size and struct format
            bytes_per_sample = bit_depth // 8
            frame_size = channels * bytes_per_sample
            fmt_char = 'h' if bit_depth == 16 else 'i'
            fmt = f'<{channels}{fmt_char}'

            # Read enough PCM for expected frames + skipped frames
            n_to_read = len(expected_frames) + skip_first
            total_bytes = n_to_read * frame_size
            pcm_buf = remainder
            while len(pcm_buf) < total_bytes:
                try:
                    chunk = sock.recv(65536)
                except socket.timeout:
                    break
                if not chunk:
                    break
                pcm_buf += chunk

            # Unpack received frames
            received = []
            for i in range(0, len(pcm_buf) - frame_size + 1, frame_size):
                samples = list(struct.unpack(fmt, pcm_buf[i:i + frame_size]))
                received.append(samples)

            # Skip warmup residual
            compare_received = received[skip_first:]

            # Compare
            n_compare = min(len(compare_received), len(expected_frames))
            max_error = 0
            mismatches = 0
            for i in range(n_compare):
                for ch in range(channels):
                    expected_val = expected_frames[i][ch] if ch < len(expected_frames[i]) else 0
                    received_val = compare_received[i][ch]
                    err = abs(received_val - expected_val)
                    max_error = max(max_error, err)
                    if err > tolerance:
                        mismatches += 1

            return {
                'pass': mismatches == 0 and n_compare == len(expected_frames),
                'handshake': handshake,
                'frames_expected': len(expected_frames),
                'frames_received': len(received),
                'frames_compared': n_compare,
                'max_error': max_error,
                'mismatches': mismatches,
                'sample_rate_derived': sample_rate,
                'error': None,
            }

        except Exception as e:
            return self._error_result(str(e), expected_frames)
        finally:
            sock.close()

    @staticmethod
    def _error_result(msg, expected_frames):
        return {
            'pass': False,
            'error': msg,
            'handshake': None,
            'frames_expected': len(expected_frames),
            'frames_received': 0,
            'frames_compared': 0,
            'max_error': 0,
            'mismatches': 0,
            'sample_rate_derived': None,
        }
