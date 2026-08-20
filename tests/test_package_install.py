"""What a user receives installs and builds - the Board Manager path.

Compiling the repository tree proves nothing about the release archive: it can
reference a file the archive does not ship, or depend on a path override that
only exists during development. This installs from a generated index into a
fresh data directory and compiles with no overrides at all.
"""
import pytest

from conftest import load

pytestmark = pytest.mark.slow

harness = load("tools/index/install_check.py", "install_check")


@pytest.fixture(scope="module")
def install(repo, gcc_bin, arduino_cli, workdir):
    # A port of its own: the harness serves the index over loopback, and the
    # default collides when several harnesses run in one session.
    return harness.run(workdir / "install", port=8741)


def test_compiles_with_no_overrides(install):
    """Both sketches built, so the installed toolchain resolved on its own."""
    assert install["sizes"]["Blink"] > 0
    # Blink touches neither Serial nor the heap - the two places where an
    # installed platform has actually differed from the working tree - so the
    # acceptance sketch is built too.
    assert install["sizes"]["Acceptance"] > 0


def test_probe_rs_installs_and_runs(install):
    """The upload path is only real if the programmer came down with it.

    This is the check that caught Windows being broken: probe-rs's Windows zip
    has no root directory and arduino-cli refuses it (ADR-0011).
    """
    assert install["probe_rs"]


def test_upgrade_and_rollback(install):
    """The index is append-only: a pinned older version stays installable."""
    current, nxt = install["versions"]
    assert current != nxt
