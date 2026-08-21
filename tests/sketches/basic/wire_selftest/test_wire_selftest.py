"""Wire behaves without a device on the bus.

The point of these is the failure path: an I2C transfer with nothing (or
nothing pulled up) on the bus has to come back with an error code instead of
spinning, because that is the state every sketch starts in when the wiring is
wrong. Talking to a real device is a wired test, not this one.

    uv run pytest sketches/basic/wire_selftest --profile ch32x035
"""
import pytest

CHECKS = [
    "nack_reported",
    "nack_bounded",
    "read_reported",
    "read_bounded",
    "read_empty",
    "overflow_truncates",
    "overflow_code",
    "overflow_skips_bus",
    "write_outside",
    "fast_mode_still_reports",
    "restart_still_reports",
]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    dut.expect_exact(f"{name} PASS")


def test_no_failures(dut) -> None:
    dut.expect_exact("wire_selftest done failures=0")
