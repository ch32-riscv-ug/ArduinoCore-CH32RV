"""The weak hooks Arduino promises a sketch can override.

Every one of these was broken once: initVariant() was declared but never
called, yield() was not weak, and serialEvent() was never dispatched because
main() did not call serialEventRun(). The sketch overrides all three, so a
regression shows up either as a link error or as a missing PASS.

Two steps, because serialEvent() needs input the command reader must not eat:
RUN does the first two checks and then stops reading, and the plain line sent
afterwards is what reaches the hook.

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")


def test_hooks_selftest(dut, ch32_uart, bench):
    kit.start(dut, 'hooks_selftest')
    uart = kit.open_uart(dut, ch32_uart, bench)
    dut.write("RUN\n")
    dut.expect_exact("initVariant_called PASS")
    dut.expect_exact("yield_called PASS")

    # The line goes out on the named UART, where only serialEvent() can take it
    # - Console is the harness and never reaches a hook. That is the check.
    dut.expect_exact("hooks_selftest send a line now")
    uart.write("a line for serialEvent\n")
    dut.expect_exact("serialEvent_called PASS")

    dut.expect_exact("hooks_selftest done failures=0")
