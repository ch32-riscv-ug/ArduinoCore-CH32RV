"""Every sketch compiles for every board its own sketch.yaml claims.

A profile is a promise that someone can build and flash that sketch on that
board. Nothing checked the promise until this existed, and it was already
broken - three sketches listed a board whose RAM their global String did not
fit in.
"""
import pytest

from conftest import run_harness

pytestmark = pytest.mark.slow


def test_all_sketch_profile_combinations(repo, gcc_bin, arduino_cli, workdir):
    output = run_harness("tests/sketches/compile_all.sh",
                         workdir / "sketch-profiles", repo)
    assert "SKETCH PROFILE COMPILE OK" in output, output[-2000:]
