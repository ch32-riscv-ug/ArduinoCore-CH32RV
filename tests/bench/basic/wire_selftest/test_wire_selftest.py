"""Wire behaves without a device on the bus.

The point of these is the failure path: an I2C transfer with nothing (or
nothing pulled up) on the bus has to come back with an error code instead of
spinning, because that is the state every sketch starts in when the wiring is
wrong. Talking to a real device is a wired test, not this one.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"wire_selftest READY" until it is asked (tests/build/sketches/testcmd.h).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")


def test_wire_selftest(dut):
    kit.start(dut, 'wire_selftest')
    dut.write("RUN\n")
    dut.expect_exact("nack_reported PASS")
    dut.expect_exact("nack_bounded PASS")
    dut.expect_exact("read_reported PASS")
    dut.expect_exact("read_bounded PASS")
    dut.expect_exact("read_empty PASS")
    dut.expect_exact("overflow_truncates PASS")
    dut.expect_exact("overflow_code PASS")
    dut.expect_exact("overflow_skips_bus PASS")
    dut.expect_exact("write_outside PASS")
    dut.expect_exact("fast_mode_still_reports PASS")
    dut.expect_exact("restart_still_reports PASS")
    dut.expect_exact("slave_accepts_no_master_calls PASS")
    dut.expect_exact("slave_quiet_unwired PASS")
    dut.expect_exact("master_after_slave PASS")
    dut.expect_exact("wire_selftest done failures=0")
