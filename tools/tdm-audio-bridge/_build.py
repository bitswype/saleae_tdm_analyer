"""Custom setuptools build backend that generates _version.py before building."""

from setuptools.build_meta import *  # noqa: F401,F403
from setuptools.build_meta import build_wheel as _build_wheel
from setuptools.build_meta import build_editable as _build_editable


def _gen_version():
    from gen_version import generate
    generate()


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    _gen_version()
    return _build_wheel(wheel_directory, config_settings, metadata_directory)


def build_editable(wheel_directory, config_settings=None, metadata_directory=None):
    _gen_version()
    return _build_editable(wheel_directory, config_settings, metadata_directory)
