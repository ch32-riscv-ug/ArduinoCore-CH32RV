"""The weak hooks Arduino promises a sketch can override.

Every one of these was broken once: initVariant() was declared but never
called, yield() was not weak, and serialEvent() was never dispatched because
main() did not call serialEventRun(). The sketch overrides all three, so a
regression shows up either as a link error or as a missing PASS.

Two steps, because serialEvent() needs input the command reader must not eat:
RUN does the first two checks and then stops reading, and the plain line sent
afterwards is what reaches the hook.
"""


def expect(console, uart) -> None:
    console.expect_exact("hooks_selftest READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("initVariant_called PASS")
    console.expect_exact("yield_called PASS")

    # The line goes out on the named UART, where only serialEvent() can take it
    # - Console is the harness and never reaches a hook. That is the check.
    console.expect_exact("hooks_selftest send a line now")
    uart.write("a line for serialEvent\n")
    console.expect_exact("serialEvent_called PASS")

    console.expect_exact("hooks_selftest done failures=0")
