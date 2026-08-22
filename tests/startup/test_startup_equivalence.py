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

import pytest

from loader import load

pytestmark = pytest.mark.slow

harness = load("tests/startup/startup_equivalence.py", "startup_equivalence")


@pytest.fixture(scope="module")
def families(repo, gcc_bin, workdir):
    root = harness.find_mirror_root()
    if root is None:
        pytest.skip("no EVT mirrors; set CH32_MIRROR_ROOT to the directory "
                    "holding the CH32* clones")
    os.environ["CH32_MIRROR_ROOT"] = root
    return harness.run(workdir / "startup")


def test_every_family_matches(families):
    differing = [tag for tag, ok in families.items() if not ok]
    assert not differing, f"these families differ from their EVT startup: {differing}"


def test_covers_the_whole_family_range(families):
    """One tag per vector layout the generator emits, so nothing is untested.

    CH32H417 is absent on purpose: it boots through loadcode, so there is no
    reset-vector behaviour to compare.
    """
    assert len(families) >= 13, sorted(families)
    for expected in ("v003", "v103", "v307_d8c", "x035"):
        assert expected in families
