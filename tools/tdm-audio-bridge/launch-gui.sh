#!/usr/bin/env bash
# Launch the TDM Audio Bridge GUI.
# Requires: pip install tools/tdm-audio-bridge/
exec python3 -c "from tdm_audio_bridge.cli import cli; cli()" gui "$@"
