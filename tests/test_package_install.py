"""What a user receives installs and builds - the Board Manager path.

Compiling the repository tree proves nothing about the release archive: it can
reference a file the archive does not ship, or depend on a path override that
only exists during development. This installs from a generated index into a
fresh data directory and compiles with no overrides at all.
"""
import pytest

from conftest import run_harness

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module")
def install(repo, gcc_bin, arduino_cli, workdir):
    # A port of its own: the harness serves the index over local HTTP, and the
    # default collides when several harnesses run in one session.
    return run_harness("tools/index/test_install.sh", workdir / "install",
                       repo, extra_env={"PORT": "8741"})


def test_installs_and_compiles_with_no_overrides(install):
    assert "INSTALL-AND-COMPILE OK" in install


def test_archive_carries_no_repository_scaffolding(install):
    """tests/, docs/ and tools/ must not reach a user's machine."""
    assert "ARCHIVE CONTENTS OK" in install


def test_probe_rs_installs_and_runs(install):
    """The upload path is only real if the programmer came down with it.

    This is the check that caught Windows being broken: probe-rs's Windows zip
    has no root directory and arduino-cli refuses it (ADR-0011).
    """
    assert "PROBE-RS INSTALL OK" in install


def test_upgrade_and_rollback(install):
    """The index is append-only: a pinned older version stays installable."""
    assert "UPGRADE AND ROLLBACK OK" in install
