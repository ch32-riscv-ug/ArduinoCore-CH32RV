"""SPI runs with nothing attached.

MISO is pulled up by begin(), so an idle bus reads 0xFF; that makes "the
transfer completed" checkable without wiring. What is really being tested is
that no combination of mode, clock or restart leaves the peripheral wedged.

    uv run pytest sketches/basic/spi_selftest --profile ch32x035
"""
import pytest

CHECKS = [
    "transfer_returns",
    "idle_high",
    "all_modes",
    "all_clocks",
    "lsb_first",
    "block_transfer",
    "transfer16",
    "legacy_api",
    "restart",
]


@pytest.mark.parametrize("name", CHECKS)
def test_check_passes(dut, name: str) -> None:
    dut.expect_exact(f"{name} PASS")


def test_no_failures(dut) -> None:
    dut.expect_exact("spi_selftest done failures=0")
