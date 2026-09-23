"""Wire behaves without a device on the bus.

The point of these is the failure path: an I2C transfer with nothing (or
nothing pulled up) on the bus has to come back with an error code instead of
spinning, because that is the state every sketch starts in when the wiring is
wrong. Talking to a real device is a wired test, not this one.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"wire_selftest READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("wire_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("nack_reported PASS")
    console.expect_exact("nack_bounded PASS")
    console.expect_exact("read_reported PASS")
    console.expect_exact("read_bounded PASS")
    console.expect_exact("read_empty PASS")
    console.expect_exact("overflow_truncates PASS")
    console.expect_exact("overflow_code PASS")
    console.expect_exact("overflow_skips_bus PASS")
    console.expect_exact("write_outside PASS")
    console.expect_exact("fast_mode_still_reports PASS")
    console.expect_exact("restart_still_reports PASS")
    console.expect_exact("slave_accepts_no_master_calls PASS")
    console.expect_exact("slave_quiet_unwired PASS")
    console.expect_exact("master_after_slave PASS")
    console.expect_exact("wire_selftest done failures=0")
