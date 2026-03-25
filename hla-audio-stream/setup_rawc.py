"""Build script for the raw C extension.

Usage:
    cd hla-audio-stream
    python setup_rawc.py build_ext --inplace
"""
from setuptools import setup, Extension

setup(
    name='tdm_decode_rawc',
    ext_modules=[
        Extension(
            '_decode_rawc',
            sources=['_decode_rawc.c'],
        ),
    ],
)
