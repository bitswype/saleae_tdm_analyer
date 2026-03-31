@echo off
REM Launch the TDM Audio Bridge GUI.
REM Requires: pip install tools/tdm-audio-bridge/
python -c "from tdm_audio_bridge.cli import cli; cli()" gui %*
if errorlevel 1 pause
