"""Shared setup for the whole test tree.

Everything is reachable from one `pytest` run. What actually runs depends on
what the machine can do, and that is decided here rather than in each test:

  pytest                      every check that needs no board and no profile
  pytest --profile ch32x035 --port /dev/ttyACM4      adds the sketch tests
  pytest -m "not slow"        skips the multi-minute compile sweeps

The build harnesses are shell scripts (they drive arduino-cli, serve a local
index, clone mirrors) and stay shell scripts. The pytest modules beside this
file run them and assert on the marker each one prints, so `pytest` is the
single entry point without rewriting working harnesses in Python.
"""
import os
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[1]


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "slow: takes minutes (the compile sweeps)")
    config.addinivalue_line(
        "markers", "hardware: needs a board attached")


def pytest_collection_modifyitems(config, items):
    """Drop the sketch tests unless a profile was asked for.

    They build and flash through a sketch profile, so without --profile there
    is nothing for them to select and pytest-embedded errors out. Silently
    collecting them would make a bare `pytest` fail on a machine that is
    perfectly able to run everything else.
    """
    if config.getoption("--profile", default=None):
        return
    sketches = REPO / "tests" / "sketches"
    skip = pytest.mark.skip(reason="needs --profile (and a board, unless "
                                   "--run-mode build)")
    for item in items:
        if sketches in pathlib.Path(str(item.fspath)).parents:
            item.add_marker(skip)


@pytest.fixture(scope="session")
def repo() -> pathlib.Path:
    return REPO


def _unavailable(what):
    """Skip - or fail, where a skip would be a false green.

    A test that quietly skips because provisioning broke looks the same as one
    that passed. On a laptop that is the right behaviour (you may not have
    fetched anything yet); in CI it hides exactly the regression the run
    exists to catch, so CH32_TESTS_REQUIRE_TOOLS turns it into a failure.
    """
    msg = f"{what} not available; run: uv run tools/index/fetch_tools.py"
    if os.environ.get("CH32_TESTS_REQUIRE_TOOLS"):
        pytest.fail(msg + "  (CH32_TESTS_REQUIRE_TOOLS is set, so this is a "
                          "failure rather than a skip)")
    pytest.skip(msg)


def _finder(name):
    import sys
    sys.path.insert(0, str(REPO / "tests" / "manual"))
    import smoke
    return getattr(smoke, name)


# One fixture per tool rather than one for all of them: a test that only needs
# the device-data tables must not skip because probe-rs is absent.
@pytest.fixture(scope="session")
def gcc_bin():
    return _finder("find_gcc_bin")() or _unavailable("the RISC-V toolchain")


@pytest.fixture(scope="session")
def probe_rs():
    return _finder("find_probe_rs")() or _unavailable("probe-rs")


@pytest.fixture(scope="session")
def tables():
    return _finder("find_tables")() or _unavailable("the ch32-device-data tables")


@pytest.fixture(scope="session")
def arduino_cli():
    if shutil.which("arduino-cli") is None:
        pytest.skip("arduino-cli is not on PATH")
    return "arduino-cli"


@pytest.fixture(scope="session")
def workdir(tmp_path_factory):
    """One scratch directory for the session, shared by the harnesses."""
    return tmp_path_factory.mktemp("harness")


def run_harness(script, work, repo, extra_env=None, timeout=3600):
    """Run one of the shell harnesses and return its combined output.

    The harnesses are chatty and slow; on failure the tail of the output is
    what identifies which part number or variant broke, so it goes into the
    assertion rather than being swallowed.
    """
    env = dict(os.environ, **(extra_env or {}))
    proc = subprocess.run([str(repo / script), str(work)],
                          cwd=repo, env=env, capture_output=True, text=True,
                          timeout=timeout)
    output = proc.stdout + proc.stderr
    if proc.returncode != 0:
        tail = "\n".join(output.strip().splitlines()[-40:])
        pytest.fail(f"{script} exited {proc.returncode}\n--- last output ---\n{tail}")
    return output
