"""The generated interrupt tables still match WCH's own startup sources.

tools/generate/interrupts/interrupts.csv is the input every vector table is
built from. It was transcribed from the EVT startup assembly, so it has to be
re-checked against those sources rather than trusted once.

Skips without the EVT mirrors, which are large clones of other repositories and
are deliberately not fetched into .tools.
"""
import subprocess

import pytest

from test_startup_equivalence import mirror_root


def test_interrupts_csv_matches_evt(repo):
    root = mirror_root()
    if root is None:
        pytest.skip("no EVT mirrors; set CH32_MIRROR_ROOT to the directory "
                    "holding the CH32* clones")
    proc = subprocess.run(
        ["uv", "run", "--no-project", "python",
         "tools/generate/import_vectors.py", "--mirrors", root, "--check"],
        cwd=repo, capture_output=True, text=True)
    assert proc.returncode == 0, (proc.stdout + proc.stderr)[-2000:]
