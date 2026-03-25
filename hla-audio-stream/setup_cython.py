"""Build script for the Cython fast decode extension.

Usage:
    cd hla-audio-stream
    python setup_cython.py build_ext --inplace
"""
from setuptools import setup
from Cython.Build import cythonize

setup(
    name='tdm_decode_fast',
    ext_modules=cythonize(
        '_decode_fast.pyx',
        compiler_directives={
            'language_level': 3,
            'boundscheck': False,
            'wraparound': False,
        },
    ),
)
