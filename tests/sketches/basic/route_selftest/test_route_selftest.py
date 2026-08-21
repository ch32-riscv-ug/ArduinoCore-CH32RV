"""setRoute()/setPins() on the monitor port itself.

Each check prints PASS or SKIP, and the SKIPs are printed by the sketch rather
than decided here: whether a series has a second route is a property of the
board, not of the test.

    uv run pytest sketches/basic/route_selftest --profile ch32x035
"""
import pytest

CHECKS = [
    "unknown_route_refused",
    "alive_after_refusal",
    "current_pins_accepted",
    "mixed_route_refused",
    "moved_to_second_route",
    "returned_to_first_route",
]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    """PASS, or a SKIP the sketch explains - never FAIL and never silence."""
    dut.expect(rf"{name} (PASS|SKIP.*)")


def test_no_failures(dut) -> None:
    dut.expect_exact("route_selftest done failures=0")
