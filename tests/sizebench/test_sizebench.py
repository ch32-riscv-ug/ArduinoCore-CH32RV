"""newlib's size on this core, per libc variant.

Not a pass/fail on absolute numbers - it is the measurement ADR-0004's choice
of newlib-nano rests on, and it belongs in the suite so the numbers stay
reproducible rather than being quoted from a one-off run. What is asserted is
the shape: nano has to be dramatically smaller than full, because that is the
claim the runtime decision is built on.
"""
import pytest

from loader import load

pytestmark = pytest.mark.slow

harness = load("tests/sizebench/sizebench.py", "sizebench")


@pytest.fixture(scope="module")
def sizes(repo, gcc_bin, workdir):
    """{(case, libc, arch): (text, data, bss)}"""
    return harness.run(workdir / "sizebench")


def test_measured_every_variant(sizes):
    assert sizes, "the harness produced no measurements"
    for libc in ("nano", "full", "nano+f"):
        assert any(k[1] == libc for k in sizes), f"no {libc} results"


@pytest.mark.parametrize("case", ["11_printf_int", "21_cpp_new"])
def test_nano_is_far_smaller_than_full(sizes, case):
    """The gap is the whole reason ADR-0004 picks newlib-nano.

    printf and operator new are where full newlib explodes: on a 16 KB
    CH32V003 the full formatter does not fit at all.
    """
    for arch in {k[2] for k in sizes}:
        nano = sizes.get((case, "nano", arch))
        full = sizes.get((case, "full", arch))
        if nano is None or full is None:
            continue
        assert full[0] > nano[0] * 4, f"{case}/{arch}: nano={nano[0]} full={full[0]}"


def test_float_printf_is_opt_in_and_costs(sizes):
    """`printf=float` in boards.txt buys %f, and it is not free."""
    for arch in {k[2] for k in sizes}:
        plain = sizes.get(("12_printf_float", "nano", arch))
        withf = sizes.get(("12_printf_float", "nano+f", arch))
        if plain is None or withf is None:
            continue
        assert withf[0] > plain[0], f"{arch}: -u _printf_float added nothing"
