@echo off
REM Launch the TDM Audio Bridge GUI without a console window.
REM Requires: pip install tools/tdm-audio-bridge/
REM
REM Uses pythonw (windowless Python) so no terminal pops up.
REM Falls back to python if pythonw is not available.
where pythonw >nul 2>nul
if %errorlevel% equ 0 (
    start "" pythonw -c "from tdm_audio_bridge.cli import cli; cli()" gui %*
) else (
    python -c "from tdm_audio_bridge.cli import cli; cli()" gui %*
    if errorlevel 1 pause
)
