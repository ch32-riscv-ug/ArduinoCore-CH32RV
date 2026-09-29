"""SPI runs with nothing attached.

MISO is pulled up by begin(), so an idle bus reads 0xFF; that makes "the
transfer completed" checkable without wiring. What is really being tested is
that no combination of mode, clock or restart leaves the peripheral wedged.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"spi_selftest READY" until it is asked (tests/sketches/testcmd.h).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest -o python_files='bench_*.py' sketches/basic --profile <profile>
"""
from loader import load

kit = load("tests/sketches/bench_kit.py", "bench_kit")


def test_spi_selftest(dut):
    kit.start(dut, 'spi_selftest')
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
