"""The unified crt0 produces the same machine state as each EVT startup.

The core owns its startup, vector table and linker script (ADR-0003) instead of
carrying WCH's per-family files. That is only safe if it lands the target in
the same state, so this builds both and compares the vector tables and the CSR
writes for every family.

Needs the EVT mirrors, which are large clones of other repositories and are
deliberately not fetched into .tools. Without them the test skips rather than
failing a machine that simply does not have them.
"""
import os
import pathlib

import pytest

from conftest import run_harness

pytestmark = pytest.mark.slow

# Where the mirrors usually sit on a bench that has them.
LIKELY = (pathlib.Path.home() / "dev_wch", pathlib.Path.home() / "mirrors")


def mirror_root():
    env = os.environ.get("CH32_MIRROR_ROOT")
    if env:
        return env
    for d in LIKELY:
        if (d / "CH32V003" / "EVT").is_dir():
            return str(d)
    return None


def test_unified_startup_matches_evt(repo, gcc_bin, workdir):
    root = mirror_root()
    if root is None:
        pytest.skip("no EVT mirrors; set CH32_MIRROR_ROOT to the directory "
                    "holding the CH32* clones")
    output = run_harness("tests/startup/run_check.sh", workdir / "startup", repo,
                         extra_env={"CH32_MIRROR_ROOT": root})
    families = output.count("=====")
    assert families >= 13, f"only {families} families were checked"
    assert "FAIL" not in output, output[-2000:]
