"""The weak hooks Arduino promises a sketch can override.

Every one of these was broken once: initVariant() was declared but never
called, yield() was not weak, and serialEvent() was never dispatched because
main() did not call serialEventRun(). The sketch overrides all three, so a
regression shows up either as a link error or as a missing PASS.

Two steps, because serialEvent() needs input the command reader must not eat:
RUN does the first two checks and then stops reading, and the plain line sent
afterwards is what reaches the hook.

    uv run pytest sketches/basic/hooks_selftest --profile ch32x035
"""


def test_hooks_selftest(dut) -> None:
    dut.expect_exact("hooks_selftest READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("initVariant_called PASS")
    dut.expect_exact("yield_called PASS")

    # From here the sketch is not reading commands, so this line reaches
    # serialEvent() instead of the command buffer. That is the check.
    dut.expect_exact("hooks_selftest send a line now")
    dut.write("a line for serialEvent\n")
    dut.expect_exact("serialEvent_called PASS")

    dut.expect_exact("hooks_selftest done failures=0")
