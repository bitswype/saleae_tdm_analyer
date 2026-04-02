"""Graphical interface for the TDM Audio Bridge.

Provides a tkinter-based window for connecting to the TDM Audio Stream
HLA and playing decoded audio, without requiring a terminal.
"""

import logging
import queue
import threading
import time as _time_mod

_time_monotonic = _time_mod.monotonic

log = logging.getLogger('tdm-audio-bridge')

tk = None
ttk = None

try:
    from ._version import VERSION, GIT_TAG, GIT_COMMITS, GIT_HASH, GIT_DIRTY
except ImportError:
    VERSION = 'unknown'
    GIT_TAG = ''
    GIT_COMMITS = '0'
    GIT_HASH = ''
    GIT_DIRTY = False


def _ensure_tk():
    """Import tkinter, raising a clear error if unavailable."""
    global tk, ttk
    if tk is not None:
        return
    try:
        import tkinter as _tk
        from tkinter import ttk as _ttk
        tk = _tk
        ttk = _ttk
    except ImportError:
        raise RuntimeError(
            "tkinter is not available.\n"
            "Install it with: apt install python3-tk (Linux)\n"
            "or ensure Python was installed with Tcl/Tk support "
            "(Windows/macOS)."
        )


# State constants
_DISCONNECTED = 'disconnected'
_CONNECTING = 'connecting'
_WAITING = 'waiting'
_BUFFERING = 'buffering'
_PLAYING = 'playing'
_STREAM_ENDED = 'stream_ended'

_STATE_COLORS = {
    _DISCONNECTED: '#888888',
    _CONNECTING: '#cc8800',
    _WAITING: '#cc8800',
    _BUFFERING: '#4488cc',
    _PLAYING: '#22aa22',
    _STREAM_ENDED: '#cc8800',
}

_STATE_LABELS = {
    _DISCONNECTED: 'Disconnected',
    _CONNECTING: 'Connecting...',
    _WAITING: 'Connected - waiting for stream',
    _BUFFERING: 'Buffering...',
    _PLAYING: 'Playing',
    _STREAM_ENDED: 'Stream ended - waiting for stream',
}


class BridgeApp:
    """Main application window."""

    def __init__(self, root, host='127.0.0.1', port=4011):
        self._root = root
        self._queue = queue.Queue()
        self._client = None
        self._player = None
        self._player_lock = threading.Lock()
        self._state = _DISCONNECTED
        self._handshake = None
        self._last_underruns = -1
        self._last_data_time = 0.0
        self._data_timeout = 2.0  # seconds with no data before "stream ended"

        root.title('TDM Audio Bridge')
        root.resizable(False, False)
        root.protocol('WM_DELETE_WINDOW', self._on_close)

        pad = {'padx': 6, 'pady': 3}

        # -- Connection frame --
        conn_frame = ttk.LabelFrame(root, text='Connection')
        conn_frame.pack(fill='x', padx=8, pady=(8, 4))

        row = ttk.Frame(conn_frame)
        row.pack(fill='x', **pad)
        ttk.Label(row, text='Host:').pack(side='left')
        self._host_var = tk.StringVar(value=host)
        ttk.Entry(row, textvariable=self._host_var, width=16).pack(
            side='left', padx=(4, 12))
        ttk.Label(row, text='Port:').pack(side='left')
        self._port_var = tk.StringVar(value=str(port))
        ttk.Entry(row, textvariable=self._port_var, width=6).pack(
            side='left', padx=(4, 0))

        btn_row = ttk.Frame(conn_frame)
        btn_row.pack(fill='x', **pad)
        self._connect_btn = ttk.Button(
            btn_row, text='Connect', command=self._connect)
        self._connect_btn.pack(side='left', padx=(0, 4))
        self._disconnect_btn = ttk.Button(
            btn_row, text='Disconnect', command=self._disconnect,
            state='disabled')
        self._disconnect_btn.pack(side='left')

        # -- Status frame --
        status_frame = ttk.LabelFrame(root, text='Status')
        status_frame.pack(fill='x', padx=8, pady=4)

        state_row = ttk.Frame(status_frame)
        state_row.pack(fill='x', **pad)
        self._state_indicator = tk.Label(
            state_row, text='\u25cf', fg='#888888',
            font=('TkDefaultFont', 14))
        self._state_indicator.pack(side='left')
        self._state_label = ttk.Label(state_row, text='Disconnected')
        self._state_label.pack(side='left', padx=(4, 0))

        info_frame = ttk.Frame(status_frame)
        info_frame.pack(fill='x', **pad)
        self._rate_label = ttk.Label(info_frame, text='Rate:   --')
        self._rate_label.pack(anchor='w')
        self._format_label = ttk.Label(info_frame, text='Format: --')
        self._format_label.pack(anchor='w')
        self._slots_label = ttk.Label(info_frame, text='Slots:  --')
        self._slots_label.pack(anchor='w')

        # -- Audio Output frame --
        audio_frame = ttk.LabelFrame(root, text='Audio Output')
        audio_frame.pack(fill='x', padx=8, pady=4)

        dev_row = ttk.Frame(audio_frame)
        dev_row.pack(fill='x', **pad)
        ttk.Label(dev_row, text='Device:').pack(side='left')
        self._device_var = tk.StringVar()
        self._device_combo = ttk.Combobox(
            dev_row, textvariable=self._device_var, state='readonly', width=30)
        self._device_combo.pack(side='left', padx=(4, 0))
        self._populate_devices()

        lat_row = ttk.Frame(audio_frame)
        lat_row.pack(fill='x', **pad)
        ttk.Label(lat_row, text='Latency:').pack(side='left')
        self._latency_var = tk.StringVar(value='high')
        ttk.Radiobutton(lat_row, text='High', variable=self._latency_var,
                        value='high').pack(side='left', padx=(4, 8))
        ttk.Radiobutton(lat_row, text='Low', variable=self._latency_var,
                        value='low').pack(side='left')

        # -- Stats frame --
        stats_frame = ttk.LabelFrame(root, text='Stats')
        stats_frame.pack(fill='x', padx=8, pady=(4, 4))

        stats_inner = ttk.Frame(stats_frame)
        stats_inner.pack(fill='x', **pad)
        self._buffer_label = ttk.Label(stats_inner, text='Buffer:    --')
        self._buffer_label.pack(anchor='w')
        self._underruns_label = ttk.Label(stats_inner, text='Underruns: 0')
        self._underruns_label.pack(anchor='w')
        self._total_underruns = 0

        # -- About button --
        bottom_row = ttk.Frame(root)
        bottom_row.pack(fill='x', padx=8, pady=(0, 8))
        self._about_btn = ttk.Button(
            bottom_row, text='About', command=self._show_about)
        self._about_btn.pack(side='right')

        # Start polling
        self._poll()

    def _populate_devices(self):
        """Fill the device dropdown with available output devices."""
        devices = [('', 'System Default')]
        try:
            from .player import list_output_devices
            for d in list_output_devices():
                devices.append((str(d['index']), d['name']))
        except RuntimeError:
            pass  # PortAudio not available
        self._device_list = devices
        self._device_combo['values'] = [name for _, name in devices]
        self._device_combo.current(0)

    def _get_selected_device(self):
        """Return the selected device index or None for default."""
        idx = self._device_combo.current()
        if idx <= 0:
            return None
        return self._device_list[idx][0]

    # -- Actions --

    def _connect(self):
        """Start the stream client and connect to the HLA."""
        from .client import StreamClient

        host = self._host_var.get().strip() or '127.0.0.1'
        try:
            port = int(self._port_var.get().strip())
        except ValueError:
            self._set_state(_DISCONNECTED, 'Invalid port')
            return

        self._set_state(_CONNECTING)
        self._connect_btn.config(state='disabled')
        self._disconnect_btn.config(state='normal')

        self._client = StreamClient(
            host=host,
            port=port,
            on_handshake=self._on_handshake,
            on_data=self._on_data,
            on_disconnect=self._on_disconnect,
            on_connected=self._on_connected,
            reconnect=True,
        )
        self._client.start()

    def _disconnect(self):
        """Stop the client and player."""
        if self._client is not None:
            self._client.stop()
            self._client = None
        with self._player_lock:
            if self._player is not None:
                self._player.stop()
                self._player = None
        self._set_state(_DISCONNECTED)
        self._connect_btn.config(state='normal')
        self._disconnect_btn.config(state='disabled')

    def _on_close(self):
        """Handle window close."""
        self._disconnect()
        self._root.destroy()

    # -- StreamClient callbacks (called from background thread) --

    def _on_connected(self):
        self._queue.put(('connected', None))

    def _on_handshake(self, handshake):
        self._queue.put(('handshake', handshake))

    def _on_data(self, data):
        with self._player_lock:
            if self._player is None:
                return
            self._player.feed(data)
            self._last_data_time = _time_monotonic()
            if self._player.is_playing and self._state != _PLAYING:
                self._queue.put(('playing', None))

    def _on_disconnect(self):
        self._queue.put(('disconnect', None))

    # -- GUI thread updates --

    def _poll(self):
        """Drain the message queue and update widgets."""
        try:
            while True:
                try:
                    msg_type, payload = self._queue.get_nowait()
                except queue.Empty:
                    break
                if msg_type == 'connected':
                    self._set_state(_WAITING)
                elif msg_type == 'handshake':
                    self._handle_handshake(payload)
                elif msg_type == 'playing':
                    self._set_state(_PLAYING)
                elif msg_type == 'disconnect':
                    # Accumulate underruns from the player before stopping it
                    with self._player_lock:
                        if self._player is not None:
                            self._total_underruns += self._player.underruns
                            self._player.stop()
                            self._player = None
                    if self._client is not None:
                        # Client will auto-reconnect. Show transitional state.
                        # _on_connected will fire if reconnect succeeds
                        # (Logic still open -> "waiting for stream").
                        # If reconnect keeps failing, we stay at this state
                        # until the user disconnects manually.
                        was_streaming = self._state in (_PLAYING, _BUFFERING)
                        if was_streaming:
                            self._set_state(_STREAM_ENDED)
                        else:
                            self._set_state(_CONNECTING)
                    else:
                        self._set_state(_DISCONNECTED)

            self._update_stats()

            # Detect stream ended: playing/buffering but no data received recently
            if self._state in (_PLAYING, _BUFFERING) and self._last_data_time > 0:
                silence = _time_monotonic() - self._last_data_time
                if silence > self._data_timeout:
                    with self._player_lock:
                        if self._player is not None:
                            self._total_underruns += self._player.underruns
                            self._player.stop()
                            self._player = None
                    self._set_state(_STREAM_ENDED)
            # Also catch buffering with no data ever received (stale HLA)
            elif self._state == _BUFFERING and self._last_data_time == 0.0:
                if hasattr(self, '_buffering_since'):
                    if _time_monotonic() - self._buffering_since > self._data_timeout:
                        with self._player_lock:
                            if self._player is not None:
                                self._player.stop()
                                self._player = None
                        self._set_state(_WAITING)

        except Exception as e:
            log.error('Error in GUI poll: %s', e, exc_info=True)
            self._set_state(_DISCONNECTED, f'Error: {e}')
        finally:
            self._root.after(100, self._poll)

    def _handle_handshake(self, handshake):
        """Process a new handshake on the GUI thread."""
        from .player import Player, find_device

        self._handshake = handshake
        self._buffering_since = _time_monotonic()
        self._last_data_time = 0.0
        self._set_state(_BUFFERING)

        self._rate_label.config(text=f'Rate:   {handshake.sample_rate} Hz')
        self._format_label.config(
            text=f'Format: {handshake.channels}ch, {handshake.bit_depth}-bit')
        self._slots_label.config(text=f'Slots:  {handshake.slot_list}')

        device = self._get_selected_device()
        try:
            device_idx = find_device(device) if device else None
        except (ValueError, RuntimeError):
            device_idx = None

        with self._player_lock:
            if self._player is not None:
                self._player.stop()
            self._player = Player(
                handshake, device=device_idx,
                latency=self._latency_var.get())
            self._player.start()

    def _update_stats(self):
        """Update live stats from the player."""
        with self._player_lock:
            if self._player is not None:
                total = self._total_underruns + self._player.underruns
                if total != self._last_underruns:
                    self._last_underruns = total
                    self._underruns_label.config(
                        text=f'Underruns: {total}')

                level = self._player.buffer_level
                pct = min(int(level * 100), 999)
                self._buffer_label.config(text=f'Buffer:    {pct}%')
            else:
                self._buffer_label.config(text='Buffer:    --')

    def _set_state(self, state, detail=''):
        """Update the state indicator and label."""
        self._state = state
        color = _STATE_COLORS.get(state, '#888888')
        label = detail or _STATE_LABELS.get(state, state)
        self._state_indicator.config(fg=color)
        self._state_label.config(text=label)

        if state == _DISCONNECTED:
            self._rate_label.config(text='Rate:   --')
            self._format_label.config(text='Format: --')
            self._slots_label.config(text='Slots:  --')

    # -- About modal --

    def _show_about(self):
        """Show the About dialog with version information."""
        about = tk.Toplevel(self._root)
        about.title('About TDM Audio Bridge')
        about.resizable(False, False)
        about.transient(self._root)
        about.grab_set()

        frame = ttk.Frame(about, padding=20)
        frame.pack(fill='both', expand=True)

        ttk.Label(frame, text='TDM Audio Bridge',
                  font=('TkDefaultFont', 14, 'bold')).pack(pady=(0, 8))

        # Version string
        ver_frame = ttk.Frame(frame)
        ver_frame.pack(pady=(0, 12))
        tk.Label(ver_frame, text=VERSION, fg='#555555',
                 font=('TkFixedFont', 11)).pack()

        # Detailed version info
        detail_frame = ttk.Frame(frame)
        detail_frame.pack(fill='x', pady=(0, 12))

        def _detail_row(parent, label, value, value_color=None):
            row = ttk.Frame(parent)
            row.pack(fill='x')
            ttk.Label(row, text=f'{label}:', width=10, anchor='e').pack(
                side='left')
            lbl = tk.Label(row, text=value, anchor='w',
                           font=('TkFixedFont', 10))
            if value_color:
                lbl.config(fg=value_color)
            lbl.pack(side='left', padx=(6, 0))

        _detail_row(detail_frame, 'Tag', GIT_TAG or '--', '#555555')
        if GIT_COMMITS != '0':
            _detail_row(detail_frame, 'Commits', f'+{GIT_COMMITS}', '#cc6600')
        else:
            _detail_row(detail_frame, 'Commits', '+0')
        _detail_row(detail_frame, 'Hash', GIT_HASH or '--', '#555555')
        if GIT_DIRTY:
            _detail_row(detail_frame, 'Dirty', 'yes', '#cc0000')
        else:
            _detail_row(detail_frame, 'Dirty', 'no')

        ttk.Button(frame, text='OK', command=about.destroy).pack()

        # Center on parent
        about.update_idletasks()
        x = self._root.winfo_x() + (self._root.winfo_width() -
                                     about.winfo_width()) // 2
        y = self._root.winfo_y() + (self._root.winfo_height() -
                                     about.winfo_height()) // 2
        about.geometry(f'+{x}+{y}')


def launch(host='127.0.0.1', port=4011):
    """Entry point for the GUI."""
    _ensure_tk()
    logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')
    log.info('tdm-audio-bridge gui %s', VERSION)
    root = tk.Tk()
    BridgeApp(root, host=host, port=port)
    root.mainloop()


if __name__ == '__main__':
    launch()
