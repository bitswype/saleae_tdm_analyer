@echo off
REM Launch the TDM Audio Bridge GUI without a console window.
REM Requires: pip install tools/tdm-audio-bridge/
REM
REM Uses pythonw (windowless Python) so no terminal pops up.
REM Falls back to python if pythonw is not available.
REM Errors are logged to %TEMP%\tdm-audio-bridge-error.log.
where pythonw >nul 2>nul
if %errorlevel% equ 0 (
    start "" pythonw -m tdm_audio_bridge gui %* 2>"%TEMP%\tdm-audio-bridge-error.log"
) else (
    python -m tdm_audio_bridge gui %*
    if errorlevel 1 pause
)
