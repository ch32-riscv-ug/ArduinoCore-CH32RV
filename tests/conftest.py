"""Shared setup for the whole test tree.

Everything is reachable from one `pytest` run. What actually runs depends on
what the machine can do, and that is decided here rather than in each test:

  pytest                      every check that needs no board and no profile
  pytest --profile ch32x035 --port /dev/ttyACM4      adds the sketch tests
  pytest -m "not slow"        skips the multi-minute compile sweeps

The harnesses are Python modules under tests/ and tools/, imported and called
directly. They used to be shell scripts invoked as subprocesses, with the tests
asserting on marker strings in their output; three Windows-only bugs later
(no shebang handling, bash 3.2 syntax, path separators) they are Python, and
the tests assert on returned values instead of parsing prose.
"""
import importlib.util
import os
import pathlib
import shutil
import tempfile

import pytest

REPO = pathlib.Path(__file__).resolve().parents[1]


def load(relative_path, name):
    """Import a harness that lives outside any package."""
    path = REPO / relative_path
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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


# Windows makes the scratch directory a correctness problem, not just a place
# to put files. arduino-cli installs the toolchain *inside* it, and GCC then
# looks for its own headers through a path it never resolves:
#
#   <tool root>/bin/../lib/gcc/riscv-none-elf/14.3.0/../../../../riscv-none-elf
#   /include/c++/14.3.0/riscv-none-elf/rv32ec/ilp32e/bits/c++config.h
#
# That tail alone is 129 characters, and GCC is a plain Win32 program with no
# long-path manifest, so once the whole thing passes 259 the open just fails.
# The diagnostic then names the canonicalised path - which is well under the
# limit and plainly exists - so the error reads as a missing file rather than
# as a too-long one. pytest's own base is
# AppData\Local\Temp\pytest-of-<user>\pytest-N\harness0, 80 characters before
# anything of ours, which leaves 50 too few.
#
# Only the Board Manager install is deep enough to care; the compile sweeps run
# the toolchain from <repo>/.tools. But one root for the session is simpler than
# a second per-harness rule, so Windows gets a short one for everything.
def _short_root() -> pathlib.Path | None:
    """A short, writable base for the session, or None to use pytest's."""
    candidates = []
    if os.environ.get("CH32_TEST_TMP"):        # escape hatch, every platform
        candidates.append(pathlib.Path(os.environ["CH32_TEST_TMP"]))
    if os.name == "nt":
        # A drive root, and nothing under the user profile: %TEMP% is already
        # C:\Users\<user>\AppData\Local\Temp, which leaves no margin at all
        # once the username is a realistic length. If this one cannot be
        # created the run should fail with install_check's explanation and a
        # pointer to CH32_TEST_TMP, not limp on from somewhere marginal.
        # An install run unpacks well over a gigabyte, so the system drive
        # first - it is normally the roomiest - then the checkout's own drive.
        drives = [os.environ.get("SystemDrive", "C:")]
        if len(REPO.drive) == 2 and REPO.drive not in drives:   # not a UNC share
            drives.append(REPO.drive)
        candidates += [pathlib.Path(d + "\\") / "ch32t" for d in drives]
    for base in candidates:
        try:
            base.mkdir(parents=True, exist_ok=True)
        except OSError:
            continue
        return base
    return None


@pytest.fixture(scope="session")
def workdir(tmp_path_factory):
    """One scratch directory for the session, shared by the harnesses."""
    base = _short_root()
    if base is None:
        yield tmp_path_factory.mktemp("harness")
        return
    # pytest keeps the last three of its own temp directories; nothing prunes
    # this one, and an install run leaves about a gigabyte behind.
    work = pathlib.Path(tempfile.mkdtemp(prefix="", dir=base))
    yield work
    if not os.environ.get("CH32_KEEP_TMP"):
        shutil.rmtree(work, ignore_errors=True)
