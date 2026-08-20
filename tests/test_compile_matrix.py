"""Every generated part number compiles, and nothing changed size unexpectedly.

The broadest cheap signal the project has: 122 part numbers across every ISA,
GPIO width and vector layout the family has. Minutes, so it is `slow`.
"""
import json
import subprocess

import pytest

from conftest import load

pytestmark = pytest.mark.slow

matrix_harness = load("tests/compile/compile_matrix.py", "compile_matrix")


@pytest.fixture(scope="module")
def matrix(repo, gcc_bin, arduino_cli, workdir):
    return matrix_harness.run(workdir / "compile-matrix")


def test_every_part_number_compiles(matrix):
    """All of them: a Failure would already have aborted, so this pins the count."""
    assert matrix["targets"] > 100, f"only {matrix['targets']} part numbers"
    assert len(matrix["sizes"]) == matrix["targets"]


def test_covers_every_board(matrix):
    assert len(matrix["boards"]) >= 20, matrix["boards"]


def test_sizes_match_the_baseline(repo, matrix):
    """A size change has to be deliberate: regenerate the baseline in the same
    change that causes it, so a surprise shows up in review."""
    baseline = json.loads(
        (repo / "tests" / "compile" / "sizes_baseline.json").read_text())
    changed = {k: (baseline.get(k), v) for k, v in matrix["sizes"].items()
               if baseline.get(k) != v}
    missing = sorted(set(baseline) - set(matrix["sizes"]))
    assert not missing, f"baseline has entries that were not built: {missing[:10]}"
    assert not changed, ("sizes changed; if intentional, regenerate with "
                         "tests/compile/check_sizes.py --update:\n"
                         + "\n".join(f"  {k}: {was} -> {now}"
                                     for k, (was, now) in list(changed.items())[:20]))
