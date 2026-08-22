"""Servo produces a real frame on the pin, with no servo attached.

The sketch times its own output pad, so "the servo library works" reduces to
"the pulse is about as wide as asked and repeats every frame" - checkable with
nothing plugged in.

A series with no timer to spare prints SKIP for every check. That is the
target's decision, not the host's, so SKIP is accepted here.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: `dut` is opened
after the flashing tool has reset the board, so the sketch repeats
"servo_selftest READY" until it is asked (tests/sketches/testcmd.h).

    uv run pytest sketches/basic/servo_selftest --profile ch32x035
"""


def test_servo_selftest(dut) -> None:
    dut.expect_exact("servo_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect(r"attach_succeeds (PASS|SKIP .*)")
    dut.expect(r"reports_attached (PASS|SKIP .*)")
    dut.expect(r"default_pulse_width (PASS|SKIP .*)")
    dut.expect(r"write_microseconds (PASS|SKIP .*)")
    dut.expect(r"write_angle_high (PASS|SKIP .*)")
    dut.expect(r"write_angle_low (PASS|SKIP .*)")
    dut.expect(r"read_back_angle (PASS|SKIP .*)")
    dut.expect(r"frame_repeats (PASS|SKIP .*)")
    dut.expect(r"detach_reported (PASS|SKIP .*)")
    dut.expect(r"detach_leaves_low (PASS|SKIP .*)")
    dut.expect_exact("servo_selftest done failures=0")
