"""Core API checks that need no external wiring.

The sketch decides pass/fail on the target and prints one line per check, so a
failure here points at a specific API rather than at a value comparison done on
the host.

    uv run pytest sketches/basic/core_api --profile ch32v203
"""
import pytest

CHECKS = [
    "millis",
    "micros",
    "digital",
    "pin_encoding",
    "analogRead",
    "adc_channel",
    "analogWrite",
    "attachInterrupt",
    "detachInterrupt",
    "shiftOut",
    "pulseIn_timeout",
    "random_repeatable",
    "random_range",
]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    dut.expect_exact(f"{name} PASS")


def test_print_formatting(dut) -> None:
    """HEX is uppercase without a prefix and floats honour the digit count."""
    dut.expect_exact("fmt=FF,-42,1.50")


def test_no_failures(dut) -> None:
    dut.expect_exact("core_api done failures=0")
