# TDM Audio Bridge

Companion CLI and GUI for the TDM Audio Stream HLA. Connects to the HLA's TCP
server and plays decoded TDM audio through any local audio device in real time.

## Install

```bash
pip install tools/tdm-audio-bridge/   # from repo root
# or
pip install tdm-audio-bridge/         # from tools/ directory
```

Requires [PortAudio](http://www.portaudio.com/) for audio output (installed
automatically on most platforms via the `sounddevice` dependency).

## Usage

### GUI

```bash
tdm-audio-bridge gui
```

Opens a tkinter window with device selection, volume control, and connection
status. Connects to `localhost:4011` by default (the Audio Stream HLA's
default port).

### CLI

```bash
# Listen and play (connects to localhost:4011)
tdm-audio-bridge listen

# Specify port and output device
tdm-audio-bridge listen --port 4011 --device 3

# List available audio output devices
tdm-audio-bridge devices
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 4011 | TCP port to connect to |
| `--device` | system default | Audio output device index (see `devices` command) |
| `-v` / `-vv` | off | Verbose / debug logging |

## How it works

1. Connects to the TCP server started by the TDM Audio Stream HLA
2. Reads a JSON handshake line (sample rate, channels, bit depth, byte order)
3. Receives raw interleaved little-endian PCM (int16 or int32)
4. Plays through the selected audio device via sounddevice/PortAudio

The client auto-reconnects if the connection drops (e.g., Logic 2 restarts a
capture).

## Architecture

```
tdm_audio_bridge/
  cli.py       Click CLI entry point (listen, gui, devices)
  gui.py       tkinter GUI (~400 lines)
  client.py    Auto-reconnecting TCP client
  player.py    sounddevice playback engine
  protocol.py  Handshake parsing and PCM unpacking
```
