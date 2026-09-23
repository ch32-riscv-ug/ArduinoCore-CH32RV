"""SPI runs with nothing attached.

MISO is pulled up by begin(), so an idle bus reads 0xFF; that makes "the
transfer completed" checkable without wiring. What is really being tested is
that no combination of mode, clock or restart leaves the peripheral wedged.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"spi_selftest READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("spi_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("transfer_returns PASS")
    console.expect_exact("idle_high PASS")
    console.expect_exact("all_modes PASS")
    console.expect_exact("all_clocks PASS")
    console.expect_exact("lsb_first PASS")
    console.expect_exact("block_transfer PASS")
    console.expect_exact("transfer16 PASS")
    console.expect_exact("legacy_api PASS")
    console.expect_exact("restart PASS")
    console.expect_exact("spi_selftest done failures=0")
