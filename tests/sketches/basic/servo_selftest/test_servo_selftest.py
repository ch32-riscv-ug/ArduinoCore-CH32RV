"""Servo produces a real frame on the pin, with no servo attached.

The sketch times its own output pad, so "the servo library works" reduces to
"the pulse is about as wide as asked and repeats every frame" - checkable with
nothing plugged in.

    uv run pytest sketches/basic/servo_selftest --profile ch32x035
"""
import pytest

CHECKS = [
    "attach_succeeds",
    "reports_attached",
    "default_pulse_width",
    "write_microseconds",
    "write_angle_high",
    "write_angle_low",
    "read_back_angle",
    "frame_repeats",
    "detach_reported",
    "detach_leaves_low",
]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    dut.expect(rf"{name} (PASS|SKIP.*)")


def test_no_failures(dut) -> None:
    dut.expect_exact("servo_selftest done failures=0")
