"""newlib's size on this core, per libc variant.

Not a pass/fail on absolute numbers - it is the measurement ADR-0004's choice
of newlib-nano rests on, and it belongs in the suite so the numbers stay
reproducible rather than being quoted from a one-off run. What is asserted is
the shape: nano has to be dramatically smaller than full, because that is the
claim the runtime decision is built on.
"""
import re

import pytest

from conftest import run_harness

pytestmark = pytest.mark.slow


def parse(output):
    """{(case, libc): text size} from the harness's markdown table."""
    out = {}
    for line in output.splitlines():
        m = re.match(r"\|\s*(\S+)\s*\|\s*(\S+)\s*\|\s*\S+\s*\|\s*(\d+)\s*\|", line)
        if m:
            out[(m.group(1), m.group(2))] = int(m.group(3))
    return out


@pytest.fixture(scope="module")
def sizes(repo, gcc_bin, workdir):
    return parse(run_harness("tests/sizebench/run_sizebench.sh",
                             workdir / "sizebench", repo))


def test_measured_every_case(sizes):
    assert sizes, "the harness produced no size table"
    for libc in ("nano", "full"):
        assert any(k[1] == libc for k in sizes), f"no {libc} results"


@pytest.mark.parametrize("case", ["11_printf_int", "21_cpp_new"])
def test_nano_is_far_smaller_than_full(sizes, case):
    """The gap is the whole reason ADR-0004 picks newlib-nano.

    printf and operator new are where full newlib explodes: on a 16 KB
    CH32V003 the full formatter does not fit at all.
    """
    nano, full = sizes.get((case, "nano")), sizes.get((case, "full"))
    if nano is None or full is None:
        pytest.skip(f"{case} not in the harness output")
    assert full > nano * 4, f"{case}: nano={nano} full={full}"
