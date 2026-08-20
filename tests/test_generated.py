"""The committed generated files still match what the generator produces.

Cheap and first: if boards.txt or a variant has drifted from the tables, every
compile result below is about a tree nobody can reproduce.
"""
import subprocess

import pytest


def test_generated_files_match_the_tables(repo, tables):
    """boards.txt, variants and vector tables regenerate byte-identical.

    The tables are checked out at the commit boards.txt records, so a
    difference here means someone hand-edited a generated file.
    """
    proc = subprocess.run(
        ["uv", "run", "--no-project", "python", "tools/generate/generate.py",
         "--tables", tables, "--platform", ".", "--check"],
        cwd=repo, capture_output=True, text=True)
    changed = [ln for ln in proc.stdout.splitlines() if not ln.startswith("ok:")]
    assert proc.returncode == 0, "\n".join(changed[-20:]) or proc.stderr[-2000:]


def test_sketch_profiles_are_in_sync(repo):
    """Every sketch.yaml matches the board list in sync_profiles.py."""
    proc = subprocess.run(
        ["uv", "run", "--no-project", "python", "tests/sketches/sync_profiles.py",
         "--check"],
        cwd=repo, capture_output=True, text=True)
    assert proc.returncode == 0, proc.stdout + proc.stderr
