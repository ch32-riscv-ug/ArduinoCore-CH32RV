"""cores/arduino/api is the pinned upstream ArduinoCore-API, unmodified.

ADR-0009 vendors a snapshot rather than a submodule, which only stays honest if
something checks it byte for byte against the commit the lock file names. A
local edit to a vendored file is the failure this catches - it would be
invisible until an upstream bump silently reverted it.

Needs network the first time (it clones upstream into the work directory).
"""
import pytest

from conftest import run_harness


def test_matches_the_pinned_commit(repo, workdir):
    output = run_harness("tools/vendor/check_api_sync.sh", workdir / "api-sync",
                         repo)
    assert "API SYNC OK" in output, output[-2000:]
