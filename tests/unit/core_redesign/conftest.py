"""Native sanitizer build shared by the logic and integrated-application tests."""

from pathlib import Path
import shutil
import subprocess

import pytest


@pytest.fixture(scope="session")
def native_control(tmp_path_factory):
    compiler = shutil.which("g++")
    if not compiler:
        pytest.fail("P2 logic checks require an explicitly installed host g++")
    destination = tmp_path_factory.mktemp("p2-native") / "control"
    result = subprocess.run([compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             "-fno-omit-frame-pointer", str(Path(__file__).with_name("host_driver.cpp")),
                             "-o", destination], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return destination
