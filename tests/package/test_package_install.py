"""What a user receives installs and builds - the Board Manager path.

Compiling the repository tree proves nothing about the release archive: it can
reference a file the archive does not ship, or depend on a path override that
only exists during development. This installs from a generated index into a
fresh data directory and compiles with no overrides at all.
"""
import os
import pathlib

import pytest

from loader import load

harness = load("tools/index/install_check.py", "install_check")


@pytest.fixture(scope="module")
def install(repo, gcc_bin, arduino_cli, workdir):
    # A port of its own: the harness serves the index over loopback, and the
    # default collides when several harnesses run in one session.
    return harness.run(workdir / "install", port=8741)


@pytest.mark.slow
def test_compiles_with_no_overrides(install):
    """Both sketches built, so the installed toolchain resolved on its own."""
    assert install["sizes"]["Blink"] > 0
    # Blink touches neither Serial nor the heap - the two places where an
    # installed platform has actually differed from the working tree - so the
    # acceptance sketch is built too.
    assert install["sizes"]["Acceptance"] > 0
    # And the bundled libraries: <Wire.h>/<SPI.h> have to resolve from the
    # installed platform, which only happens if libraries/ is in the archive.
    assert install["sizes"]["Libraries"] > 0


@pytest.mark.slow
def test_probe_rs_installs_and_runs(install):
    """The upload path is only real if the programmer came down with it.

    This is the check that caught Windows being broken: probe-rs's Windows zip
    has no root directory and arduino-cli refuses it (ADR-0011).
    """
    assert install["probe_rs"]


@pytest.mark.slow
def test_upgrade_and_rollback(install):
    """The index is append-only: a pinned older version stays installable."""
    current, nxt = install["versions"]
    assert current != nxt


def test_path_budget_rejects_a_deep_sandbox(monkeypatch):
    """The Windows depth check fires before the confusing failure does.

    Too deep, and the real symptom is "bits/c++config.h: No such file or
    directory" for a file that is right there: GCC opens its include path with
    the dot-dots unresolved but canonicalises it for the message. The check is
    string arithmetic over a path that need not exist, and a separator counts
    as one character either way, so it is exact on any host.
    """
    monkeypatch.setattr(os, "name", "nt")
    deep = pathlib.PurePath(r"C:\Users\runneradmin\AppData\Local\Temp"
                            r"\pytest-of-runneradmin\pytest-0\harness0\install")
    with pytest.raises(harness.Failure, match="too deep for Windows"):
        harness.check_path_budget(deep)


def test_path_budget_accepts_the_short_root(monkeypatch):
    """What conftest hands over on Windows: a drive root plus eight characters."""
    monkeypatch.setattr(os, "name", "nt")
    harness.check_path_budget(pathlib.PurePath(r"C:\ch32t\a1b2c3d4\install"))
