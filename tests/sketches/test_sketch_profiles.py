"""Every sketch compiles for every board its own sketch.yaml claims.

A profile is a promise that someone can build and flash that sketch on that
board. Nothing checked the promise until this existed, and it was already
broken - three sketches listed a board whose 2 KB of RAM their global String
could never fit in.
"""
import pytest

from loader import load

pytestmark = pytest.mark.slow

harness = load("tests/sketches/compile_all.py", "compile_all")


@pytest.fixture(scope="module")
def built(repo, gcc_bin, arduino_cli, workdir):
    return harness.run(workdir / "sketch-profiles")


def test_every_combination_compiles(built):
    assert built, "no sketch/board combinations were found at all"


def test_every_sketch_is_covered(repo, built):
    """Each sketch directory appears, so none silently lost its profiles."""
    on_disk = {p.parent.name for p in
               (repo / "tests" / "sketches").glob("*/*/sketch.yaml")}
    assert {name for name, _ in built} == on_disk
