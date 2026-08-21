"""tone() drives the pin, at about the right rate, and stops when told.

No speaker is involved: the sketch reads back the pad it is driving, which is
enough to tell playing from silent and to catch a timer left running after
noTone() or after a duration expired.

    uv run pytest sketches/basic/tone_selftest --profile ch32x035
"""
import pytest

CHECKS = [
    "tone_toggles_pin",
    "tone_rate_plausible",
    "invalid_pin_ignored",
    "notone_stops",
    "notone_leaves_low",
    "duration_plays",
    "duration_stops",
    "duration_leaves_low",
    "restart_plays",
]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    dut.expect(rf"{name} (PASS|SKIP.*)")


def test_no_failures(dut) -> None:
    dut.expect_exact("tone_selftest done failures=0")
