"""The weak hooks Arduino promises a sketch can override.

Every one of these was broken once: initVariant() was declared but never
called, yield() was not weak, and serialEvent() was never dispatched because
main() did not call serialEventRun(). The sketch overrides all three, so a
regression shows up either as a link error or as a missing PASS.

    uv run pytest sketches/basic/hooks_selftest --profile ch32x035
"""
import pytest

CHECKS = ["initVariant_called", "yield_called"]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    dut.expect_exact(f"{name} PASS")


def test_serial_event_is_dispatched(dut) -> None:
    """serialEvent() needs input, so the test provides it."""
    dut.expect_exact("hooks_selftest send a line now")
    dut.write("ping\n")
    dut.expect_exact("serialEvent_called PASS")


def test_no_failures(dut) -> None:
    dut.expect_exact("hooks_selftest done failures=0")
