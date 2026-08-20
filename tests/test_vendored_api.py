"""cores/arduino/api is the pinned upstream ArduinoCore-API, unmodified.

ADR-0009 vendors a snapshot rather than a submodule, which only stays honest if
something checks it byte for byte against the commit the lock file names. A
local edit to a vendored file is the failure this catches - it would be
invisible until an upstream bump silently reverted it.

Needs network the first time (it clones upstream into the work directory).
"""
import pytest

from conftest import load

harness = load("tools/vendor/check_api_sync.py", "check_api_sync")


@pytest.fixture(scope="module")
def synced(repo, workdir):
    return harness.run(workdir / "api-sync")


def test_matches_the_pinned_commit(synced):
    assert synced["files"] > 0
    assert synced["tag"]


def test_api_version_matches_the_pin(synced):
    """A version bump has to move both the lock and the vendored header."""
    assert synced["api_version"] >= 10502
