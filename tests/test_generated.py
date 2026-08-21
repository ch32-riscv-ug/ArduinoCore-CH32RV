"""The committed generated files still match what the generator produces.

Cheap and first: if boards.txt or a variant has drifted from the tables, every
compile result below is about a tree nobody can reproduce.
"""
import re
import subprocess

import pytest


def test_generated_files_match_the_tables(repo, tables):
    """boards.txt, variants and vector tables regenerate byte-identical.

    The tables are checked out at the commit vendor/ch32-device-data.lock.toml
    records, so a difference here means someone hand-edited a generated file.
    The lock is itself one of the generated files, so a stale pin - a bumped
    commit whose table hashes no longer match - fails here too.
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


def test_every_table_read_goes_through_read_table(repo):
    """The lock can only list inputs it is told about.

    gen_lock() hashes what read_table() recorded, and the claim the lock makes
    - "an upstream commit that touches none of these cannot change a generated
    file" - is false the moment some loader opens a CSV directly. There is no
    way to notice that at runtime, so it is checked in the source instead.
    """
    src = (repo / "tools" / "generate" / "generate.py").read_text(encoding="utf-8")
    # A literal name here means the loader bypassed read_table().
    stray = re.findall(r'open\((?:args\.)?tables\s*/\s*"[^"]+"', src)
    assert not stray, ("read the tables through read_table() so they reach "
                       "vendor/ch32-device-data.lock.toml: " + repr(stray))
