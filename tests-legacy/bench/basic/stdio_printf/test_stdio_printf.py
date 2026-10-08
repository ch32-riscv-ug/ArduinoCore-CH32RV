"""printf()/puts()/write() reach the serial monitor.

Both bugs this guards against were silent: the sketch either reset before
printing anything (libgloss's semihosting _write) or printed nothing while
returning success (the printf bridge compiled to `return 0`). So what is checked
is that the output arrived, not that the call returned.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"stdio_printf READY" until it is asked (tests-legacy/build/sketches/testcmd.h).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")


def test_stdio_printf(dut):
    kit.start(dut, 'stdio_printf')
    dut.write("RUN\n")
    dut.expect_exact("stdio test start")
    # write(2) must go to the UART, not to a semihosting ecall.
    dut.expect_exact("write=direct")
    dut.expect_exact("write returned 14")
    dut.expect_exact("printf=42 str x")
    # 0 was the symptom of the bridge dropping every byte.
    dut.expect_exact("printf returned 17")
    dut.expect_exact("puts=line")
    dut.expect_exact("puts returned ok")
    # Wide enough to need the buffer newlib mallocs for stdout.
    dut.expect_exact("wide=deadbeef")
    dut.expect_exact("stdio test done")
    dut.expect_exact("stdio_printf done failures=0")
