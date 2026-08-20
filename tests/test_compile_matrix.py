"""Every generated part number compiles, and nothing changed size unexpectedly.

This is the broadest cheap signal the project has: 122 part numbers across
every ISA, GPIO width and vector layout the family has. It takes minutes, so
it carries the `slow` marker.
"""
import json
import subprocess

import pytest

from conftest import run_harness

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module")
def matrix(repo, gcc_bin, arduino_cli, workdir):
    work = workdir / "compile-matrix"
    output = run_harness("tests/compile/test_compile.sh", work, repo)
    return work, output


def test_every_part_number_compiles(matrix):
    work, output = matrix
    compiled = output.count("== compile Blink for ")
    assert compiled > 100, f"only {compiled} part numbers were built"
    assert (work / "sizes.tsv").exists()


def test_user_flags_reach_the_compiler(matrix):
    """ADR-0007: build.extra_flags is the user's injection point."""
    _, output = matrix
    assert "EXTRA_FLAGS INJECTION OK" in output


def test_sizes_match_the_baseline(repo, matrix):
    """A size change has to be deliberate: regenerate the baseline in the same
    change that causes it, so a surprise shows up in review."""
    work, _ = matrix
    proc = subprocess.run(
        ["uv", "run", "--no-project", "python", "tests/compile/check_sizes.py",
         "--baseline", "tests/compile/sizes_baseline.json",
         "--sizes", str(work / "sizes.tsv"), "--check"],
        cwd=repo, capture_output=True, text=True)
    diffs = [ln for ln in proc.stdout.splitlines() if ln.startswith("DIFF")]
    assert proc.returncode == 0, "\n".join(diffs[:20]) or proc.stdout
