"""Shared setup for the whole test tree.

Everything is reachable from one `pytest` run. What actually runs depends on
what the machine can do, and that is decided here rather than in each test:

  pytest                      every check that needs no board (unit, build)
  pytest --clean              the same, with every cache cleared first
  pytest -m "not slow"        skips the multi-minute compile sweeps
  pytest --sweep              adds the every-example x every-series sweep
  pytest bench --profile <b>  the bench tests, on the bench behind that profile

Collection is by directory, cut by what a check needs (unit, build, bench,
manual). bench/ and manual/ are in norecursedirs and manual's entry points carry
no test_ prefix, so nothing a bare `pytest` picks up ever flashes a board.
See docs/development-workflow.ja.md and tests/TEST_PLAN.ja.md.

Kept small on purpose, and nothing here touches a board. Hardware runs are
`pytest bench --profile <board>`, whose own conftest checks the bench first. Shared
code that is not a fixture goes in a normally-named module beside this one
(loader.py), because a second collected conftest.py would replace this module in
sys.modules.

The harnesses are Python modules under tests/ and tools/, imported and called
directly. They used to be shell scripts invoked as subprocesses, with the tests
asserting on marker strings in their output; three Windows-only bugs later
(no shebang handling, bash 3.2 syntax, path separators) they are Python, and
the tests assert on returned values instead of parsing prose.
"""
import os
import argparse
import pathlib
import shutil
import tempfile

import pytest

from loader import REPO, find_gcc_bin, find_tables


def _clean_requested(config) -> bool:
    return bool(config.getoption("--clean", default=False))


def _clean(config):
    """Remove what a previous run left behind, and say what went.

    Deliberately narrow. Every harness already builds into a scratch directory
    of its own and sets ARDUINO_DIRECTORIES_* alongside it, so almost nothing
    survives a run - what does is pytest's caches and, when a run crashed or
    CH32_KEEP_TMP was set, a scratch directory of a gigabyte or so.

    Three things are left alone on purpose, because deleting them turns a
    two-minute run into an hour and does not make it any more honest:
      <repo>/.tools        provisioned toolchain and probe-rs, checksum-pinned
      ~/.arduino15         arduino-cli's own data directory
      pytest's tmp base    shared with every other project on the machine
    A run that has to re-fetch tools is `uv run tools/index/fetch_tools.py`.
    """
    here = pathlib.Path(__file__).resolve().parent
    targets = [here / ".pytest_cache", *here.rglob("__pycache__")]
    # Scratch roots this suite creates itself: the contents go, the root stays.
    base = _short_root()
    if base is not None:
        targets += sorted(base.iterdir())
    removed = 0
    for path in targets:
        if not path.exists() or ".venv" in path.parts:
            continue
        shutil.rmtree(path, ignore_errors=True)
        removed += 1
    config.stash[_CLEANED] = removed


_CLEANED = pytest.StashKey[int]()


def pytest_addoption(parser):
    # pytest-embedded-arduino-cli (a bench run adds it with `uv run --with`) owns
    # --clean already, as "arduino-cli compile --clean"; this conftest rides on it
    # and clears the caches too. Declare it only when that plugin is not loaded.
    try:
        parser.addoption(
            "--clean", action="store_true", default=False,
            help="clear pytest's caches and this suite's scratch directories first")
    except (ValueError, argparse.ArgumentError):
        pass
    parser.addoption(
        "--sweep", action="store_true", default=False,
        help="run the example sweep (every example x every series, ~20 min). "
             "Meant for GitHub Actions, where the wall-clock is nobody's.")
    parser.addoption(
        "--sweep-shard", default=None, metavar="K/N",
        help="build only every N-th series of the sweep, starting at the K-th (1-based): "
             "CI runs the shards as parallel jobs. Without it, every series.")


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "slow: takes minutes (the compile sweeps)")
    config.addinivalue_line(
        "markers", "sweep: builds every example for every series; needs --sweep")
    config.addinivalue_line(
        "markers", "hardware: needs a board attached")
    if _clean_requested(config):
        _clean(config)


def pytest_report_header(config):
    """Say that --clean ran, so a fast suite is not mistaken for a cached one."""
    if _clean_requested(config):
        return f"clean: removed {config.stash[_CLEANED]} cache/scratch directories"
    return None


def pytest_collection_modifyitems(config, items):
    """Skip the example sweep unless it was asked for."""
    if not config.getoption("--sweep", default=False):
        # Opt-in, not opt-out: 20 minutes is fine on a runner and not fine in
        # front of someone waiting. docs/examples-build-rules.ja.md.
        no_sweep = pytest.mark.skip(reason="needs --sweep (~20 min)")
        for item in items:
            if item.get_closest_marker("sweep"):
                item.add_marker(no_sweep)


@pytest.fixture(scope="session")
def repo() -> pathlib.Path:
    return REPO


@pytest.fixture(scope="session")
def sweep_boards(request) -> tuple:
    """The series the example sweep builds in this process: all of them, or the shard --sweep-shard K/N
    names - every N-th series in boards.txt order starting at the K-th, so the heavy and the light series
    spread over the shards. (boards, sharded?)"""
    from sketch_requirements import all_boards
    boards = all_boards()
    shard = request.config.getoption("--sweep-shard")
    if not shard:
        return boards, False
    try:
        k, n = (int(x) for x in shard.split("/"))
        if not 1 <= k <= n:
            raise ValueError
    except ValueError:
        raise pytest.UsageError(f"--sweep-shard wants K/N with 1 <= K <= N, not {shard!r}") from None
    return boards[k - 1::n], True


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


# One fixture per tool rather than one for all of them: a test that only needs
# the device-data tables must not skip because the toolchain is absent.
@pytest.fixture(scope="session")
def gcc_bin():
    return find_gcc_bin() or _unavailable("the RISC-V toolchain")


@pytest.fixture(scope="session")
def tables():
    return find_tables() or _unavailable("the ch32-device-data tables")


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
