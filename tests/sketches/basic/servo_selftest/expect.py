"""Servo produces a real frame on the pin, with no servo attached.

The sketch times its own output pad, so "the servo library works" reduces to
"the pulse is about as wide as asked and repeats every frame" - checkable with
nothing plugged in.

A series with no timer to spare prints SKIP for every check. That is the
target's decision, not the host's, so SKIP is accepted here.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"servo_selftest READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("servo_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"attach_succeeds (PASS|SKIP .*)")
    console.expect(r"reports_attached (PASS|SKIP .*)")
    console.expect(r"default_pulse_width (PASS|SKIP .*)")
    console.expect(r"write_microseconds (PASS|SKIP .*)")
    console.expect(r"write_angle_high (PASS|SKIP .*)")
    console.expect(r"write_angle_low (PASS|SKIP .*)")
    console.expect(r"read_back_angle (PASS|SKIP .*)")
    console.expect(r"frame_repeats (PASS|SKIP .*)")
    console.expect(r"detach_reported (PASS|SKIP .*)")
    console.expect(r"detach_leaves_low (PASS|SKIP .*)")
    console.expect_exact("servo_selftest done failures=0")
