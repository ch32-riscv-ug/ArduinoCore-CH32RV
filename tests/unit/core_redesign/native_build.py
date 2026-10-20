"""Native sanitizer build shared by the logic and integrated-application tests.

An ordinary module, not a conftest.py: tests/ keeps no conftest (unit/test_workspace.py). Each test file loads
it with importlib like its other siblings and wraps `build()` in its own fixture; the executable is built once
per run."""

from pathlib import Path
import shutil
import subprocess

import pytest

_built = None


def build(tmp_path_factory):
    global _built
    if _built is not None:
        return _built
    compiler = shutil.which("g++")
    if not compiler:
        pytest.fail("P2 logic checks require an explicitly installed host g++")
    destination = tmp_path_factory.mktemp("p2-native") / "control"
    result = subprocess.run([compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             "-fno-omit-frame-pointer", str(Path(__file__).with_name("host_driver.cpp")),
                             "-o", destination], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    _built = destination
    return destination
