"""SPI runs with nothing attached.

MISO is pulled up by begin(), so an idle bus reads 0xFF; that makes "the
transfer completed" checkable without wiring. What is really being tested is
that no combination of mode, clock or restart leaves the peripheral wedged.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: `dut` is opened
after the flashing tool has reset the board, so the sketch repeats
"spi_selftest READY" until it is asked (tests/sketches/testcmd.h).

    uv run pytest sketches/basic/spi_selftest --profile ch32x035
"""


def test_spi_selftest(dut) -> None:
    dut.expect_exact("spi_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("transfer_returns PASS")
    dut.expect_exact("idle_high PASS")
    dut.expect_exact("all_modes PASS")
    dut.expect_exact("all_clocks PASS")
    dut.expect_exact("lsb_first PASS")
    dut.expect_exact("block_transfer PASS")
    dut.expect_exact("transfer16 PASS")
    dut.expect_exact("legacy_api PASS")
    dut.expect_exact("restart PASS")
    dut.expect_exact("spi_selftest done failures=0")
