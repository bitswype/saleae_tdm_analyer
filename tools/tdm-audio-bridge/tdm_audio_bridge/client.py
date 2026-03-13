"""TCP client that connects to the TDM Audio Stream HLA and receives PCM data.

Handles connection, handshake, reconnection on drops, and provides a
callback-based interface for consuming PCM frames.
"""

import socket
import threading
import logging

from .protocol import read_handshake

log = logging.getLogger('tdm-audio-bridge')


class StreamClient:
    """Connects to the HLA's TCP server and delivers raw PCM chunks.

    Runs a background receive thread. Calls on_handshake(handshake) when
    connected and on_data(bytes) for each received chunk. On disconnect,
    reconnects automatically unless stopped.
    """

    def __init__(self, host='127.0.0.1', port=4011,
                 on_handshake=None, on_data=None, on_disconnect=None,
                 reconnect=True, reconnect_delay=1.0):
        self._host = host
        self._port = port
        self._on_handshake = on_handshake
        self._on_data = on_data
        self._on_disconnect = on_disconnect
        self._reconnect = reconnect
        self._reconnect_delay = reconnect_delay
        self._stop = threading.Event()
        self._thread = None
        self._handshake = None

    @property
    def handshake(self):
        return self._handshake

    def start(self):
        """Start the receive loop in a background thread."""
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        """Signal the client to stop and wait for the thread to exit."""
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=5.0)
            self._thread = None

    def _run(self):
        """Main receive loop with auto-reconnect."""
        while not self._stop.is_set():
            sock = None
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(self._reconnect_delay)
                sock.connect((self._host, self._port))
                sock.settimeout(1.0)
                log.info("Connected to %s:%d", self._host, self._port)

                # Read handshake
                self._handshake, remainder = read_handshake(sock)
                if self._on_handshake:
                    self._on_handshake(self._handshake)

                # Deliver any PCM data that arrived with the handshake
                if remainder:
                    if self._on_data:
                        self._on_data(remainder)

                # Receive loop
                while not self._stop.is_set():
                    try:
                        chunk = sock.recv(65536)
                        if not chunk:
                            log.warning("Server closed connection")
                            break
                        if self._on_data:
                            self._on_data(chunk)
                    except socket.timeout:
                        continue

            except (ConnectionRefusedError, OSError) as e:
                if not self._stop.is_set():
                    log.debug("Connection attempt failed: %s", e)
            finally:
                if sock:
                    try:
                        sock.close()
                    except OSError:
                        pass
                self._handshake = None
                if self._on_disconnect:
                    self._on_disconnect()

            if not self._reconnect or self._stop.is_set():
                break

            # Wait before reconnecting
            self._stop.wait(timeout=self._reconnect_delay)
