#!/usr/bin/env python3
"""System check for the TDM Analyzer streaming setup.

Validates that all components are installed and configured correctly for
real-time audio streaming. Run this from the repo root (or release zip root):

    python check_setup.py

Each check prints PASS, WARN, or FAIL with an actionable message.
"""

import sys
import os
import shutil
import platform
import importlib
import subprocess


PASS = "\033[92mPASS\033[0m"
WARN = "\033[93mWARN\033[0m"
FAIL = "\033[91mFAIL\033[0m"

pass_count = 0
warn_count = 0
fail_count = 0


def check(ok, label, fix=""):
    global pass_count, warn_count, fail_count
    if ok:
        print(f"  [{PASS}] {label}")
        pass_count += 1
    else:
        print(f"  [{FAIL}] {label}")
        if fix:
            print(f"         -> {fix}")
        fail_count += 1


def warn(label, msg=""):
    global warn_count
    print(f"  [{WARN}] {label}")
    if msg:
        print(f"         -> {msg}")
    warn_count += 1


def section(title):
    print(f"\n{title}")
    print("-" * len(title))


def main():
    print("TDM Analyzer - System Check")
    print("=" * 40)

    # ---------------------------------------------------------------
    section("Python")
    # ---------------------------------------------------------------

    v = sys.version_info
    check(v >= (3, 9), f"Python {v.major}.{v.minor}.{v.micro}",
          "Python 3.9+ required. Download from python.org")

    # Check pythonw on Windows
    if platform.system() == "Windows":
        pythonw = shutil.which("pythonw")
        check(pythonw is not None,
              f"pythonw found: {pythonw}" if pythonw else "pythonw not found",
              "pythonw is needed for the GUI launcher (no console window). "
              "It should be installed with Python - check your PATH.")

    # ---------------------------------------------------------------
    section("TDM Audio Bridge (pip package)")
    # ---------------------------------------------------------------

    try:
        import tdm_audio_bridge
        version = getattr(tdm_audio_bridge, '__version__', None)
        if version is None:
            try:
                from tdm_audio_bridge._version import VERSION
                version = VERSION
            except ImportError:
                version = "unknown"
        check(True, f"tdm-audio-bridge installed: {version}")
    except ImportError:
        check(False, "tdm-audio-bridge not installed",
              "pip install tools/tdm-audio-bridge/")

    # ---------------------------------------------------------------
    section("Audio dependencies")
    # ---------------------------------------------------------------

    # sounddevice
    try:
        import sounddevice as sd
        check(True, f"sounddevice {sd.__version__}")
    except ImportError:
        check(False, "sounddevice not installed",
              "pip install sounddevice")
    except OSError as e:
        check(False, f"sounddevice import failed: {e}",
              "PortAudio library not found. "
              "Linux: sudo apt install libportaudio2  "
              "macOS: brew install portaudio  "
              "Windows: should be bundled with sounddevice")

    # numpy
    try:
        import numpy as np
        check(True, f"numpy {np.__version__}")
    except ImportError:
        check(False, "numpy not installed", "pip install numpy")

    # List audio devices
    try:
        import sounddevice as sd
        devices = sd.query_devices()
        output_devs = [d for d in devices if d['max_output_channels'] > 0]
        check(len(output_devs) > 0,
              f"{len(output_devs)} audio output device(s) found")
        for d in output_devs[:5]:
            idx = devices.index(d) if hasattr(devices, 'index') else '?'
            print(f"           [{idx}] {d['name']} ({d['max_output_channels']}ch)")
        if len(output_devs) > 5:
            print(f"           ... and {len(output_devs) - 5} more")
    except Exception:
        pass

    # ---------------------------------------------------------------
    section("Click CLI framework")
    # ---------------------------------------------------------------

    try:
        import click
        try:
            from importlib.metadata import version as pkg_version
            click_ver = pkg_version("click")
        except Exception:
            click_ver = getattr(click, '__version__', 'unknown')
        check(True, f"click {click_ver}")
    except ImportError:
        check(False, "click not installed", "pip install click")

    # ---------------------------------------------------------------
    section("HLA extensions")
    # ---------------------------------------------------------------

    repo_root = os.path.dirname(os.path.abspath(__file__))

    hla_stream = os.path.join(repo_root, "hla-audio-stream", "TdmAudioStream.py")
    check(os.path.exists(hla_stream),
          "Audio Stream HLA found" if os.path.exists(hla_stream)
          else "Audio Stream HLA not found",
          f"Expected at {hla_stream}")

    hla_wav = os.path.join(repo_root, "hla-wav-export", "TdmWavExport.py")
    check(os.path.exists(hla_wav),
          "WAV Export HLA found" if os.path.exists(hla_wav)
          else "WAV Export HLA not found",
          f"Expected at {hla_wav}")

    # Cython extension
    ext_suffix = ".pyd" if platform.system() == "Windows" else ".so"
    cython_path = os.path.join(repo_root, "hla-audio-stream")
    cython_files = [f for f in os.listdir(cython_path)
                    if f.startswith("_decode_fast") and f.endswith(ext_suffix)]
    if cython_files:
        check(True, f"Cython fast decoder: {cython_files[0]}")
    else:
        warn("Cython fast decoder not compiled (optional)",
             "cd hla-audio-stream && pip install cython && "
             "python setup_cython.py build_ext --inplace")

    # ---------------------------------------------------------------
    section("LLA plugin (C++ DLL)")
    # ---------------------------------------------------------------

    if platform.system() == "Windows":
        # Check common install locations
        dll_name = "tdm_analyzer.dll"
        common_paths = [
            os.path.join(os.path.expanduser("~"), "Documents", "installers",
                         "saleae", "custom_analyzers", dll_name),
            os.path.join(repo_root, "build", "Analyzers", "Release", dll_name),
        ]
        found = None
        for p in common_paths:
            if os.path.exists(p):
                found = p
                break
        if found:
            check(True, f"LLA DLL found: {found}")
        else:
            warn("LLA DLL not found in common locations",
                 "Build with: cmake -B build -A x64 && "
                 "cmake --build build --config Release\n"
                 "         Then copy build\\Analyzers\\Release\\tdm_analyzer.dll "
                 "to your Logic 2 custom analyzers directory.")
    elif platform.system() == "Linux":
        so_path = os.path.join(repo_root, "build", "Analyzers",
                               "libtdm_analyzer.so")
        if os.path.exists(so_path):
            check(True, f"LLA .so found: {so_path}")
        else:
            warn("LLA .so not found",
                 "Build with: cmake -B build -DCMAKE_BUILD_TYPE=Release && "
                 "cmake --build build")
    elif platform.system() == "Darwin":
        so_path = os.path.join(repo_root, "build", "arm64", "Analyzers",
                               "libtdm_analyzer.so")
        if os.path.exists(so_path):
            check(True, f"LLA .so found: {so_path}")
        else:
            warn("LLA .so not found",
                 "Build with: cmake -B build/arm64 -DCMAKE_BUILD_TYPE=Release "
                 "-DCMAKE_OSX_ARCHITECTURES=arm64 && cmake --build build/arm64")

    # ---------------------------------------------------------------
    section("Network")
    # ---------------------------------------------------------------

    import socket

    def _check_port(port, timeout=0.3):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        try:
            s.connect(('127.0.0.1', port))
            s.close()
            return True
        except (ConnectionRefusedError, OSError):
            return False

    if _check_port(4011):
        check(True, "TCP port 4011 - HLA server is listening")
    else:
        warn("TCP port 4011 - nothing listening (normal if not capturing)",
             "Start a capture with the Audio Stream HLA to start the server")

    # ---------------------------------------------------------------
    section("Logic 2 automation")
    # ---------------------------------------------------------------

    if _check_port(10530):
        check(True, "Logic 2 automation API responding on port 10530")
    else:
        warn("Logic 2 automation not detected on port 10530",
             "Open Logic 2 and enable automation in Preferences")

    # ---------------------------------------------------------------
    # Summary
    # ---------------------------------------------------------------

    print(f"\n{'=' * 40}")
    print(f"Results: {pass_count} passed, {warn_count} warnings, "
          f"{fail_count} failed")
    if fail_count > 0:
        print("\nFix the FAIL items above before attempting to stream.")
    elif warn_count > 0:
        print("\nWARN items are optional but recommended.")
    else:
        print("\nAll checks passed. Ready to stream!")

    return 1 if fail_count > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
