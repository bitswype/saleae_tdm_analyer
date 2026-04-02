#!/usr/bin/env bash
# Launch the TDM Audio Bridge GUI.
# Requires: pip install tools/tdm-audio-bridge/
exec python3 -m tdm_audio_bridge gui "$@"
