"""Lifetime, deferred-IRQ boundary and host thread comparison contracts."""
from pathlib import Path
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def native_shared(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp02-shared") / "control"
    compiler = shutil.which("g++")
    assert compiler, "host g++ required"
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-pthread", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("shared_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, mode, seed):
    result = subprocess.run([executable, mode, str(seed)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stdout and not result.stderr


@pytest.mark.parametrize("seed", range(10))
def test_owned_lifetime_and_full_rejection(native_shared, seed):
    run(native_shared, "owned", seed)


@pytest.mark.parametrize("point", range(5))
def test_arrival_at_wait_registration_boundary(native_shared, point):
    run(native_shared, "irq", point)


@pytest.mark.parametrize("seed", range(4))
def test_four_producers_with_explicit_try_lock(native_shared, seed):
    run(native_shared, "mpsc", seed)


@pytest.mark.parametrize("mode", ["spsc1", "spsc4", "spsc64"])
@pytest.mark.parametrize("seed", range(4))
def test_spsc_publication_and_counter_wrap(native_shared, mode, seed):
    run(native_shared, mode, seed)
