"""gpio_loopback's conversation, replayed by smoke.run_directory().

Every check has to PASS. A missing jumper fails `level_through_wire`, not the
banner: the sketch drives both levels, because a floating input often reads HIGH.
The first line after RUN names the pads the sketch was compiled with, which is the
first thing to look at when a check fails.
"""


def expect(console) -> None:
    console.expect_exact("gpio_loopback READY", timeout=20)
    console.write("RUN\n")
    console.expect(r"out=\d+ in=\d+")
    console.expect_exact("pins_differ PASS")
    console.expect_exact("pins_valid PASS")
    console.expect_exact("level_through_wire PASS")
    console.expect_exact("pullup PASS")
    console.expect_exact("pulldown PASS")
    console.expect_exact("exti_cross_port PASS")
    console.expect_exact("pwm_duty_25pct PASS")
    console.expect_exact("pwm_duty_75pct PASS")
    console.expect_exact("pwm_duty_ordered PASS")
    console.expect_exact("gpio_loopback done failures=0")
