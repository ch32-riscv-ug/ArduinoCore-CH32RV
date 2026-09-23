"""printf()/puts()/write() reach the serial monitor.

Both bugs this guards against were silent: the sketch either reset before
printing anything (libgloss's semihosting _write) or printed nothing while
returning success (the printf bridge compiled to `return 0`). So what is checked
is that the output arrived, not that the call returned.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: the runner attaches after
the board has been flashed and reset, so the sketch repeats
"stdio_printf READY" until it is asked (tests/sketches/testcmd.h).
"""


def expect(console) -> None:
    console.expect_exact("stdio_printf READY", timeout=20)
    console.write("RUN\n")
    console.expect_exact("stdio test start")
    # write(2) must go to the UART, not to a semihosting ecall.
    console.expect_exact("write=direct")
    console.expect_exact("write returned 14")
    console.expect_exact("printf=42 str x")
    # 0 was the symptom of the bridge dropping every byte.
    console.expect_exact("printf returned 17")
    console.expect_exact("puts=line")
    console.expect_exact("puts returned ok")
    # Wide enough to need the buffer newlib mallocs for stdout.
    console.expect_exact("wide=deadbeef")
    console.expect_exact("stdio test done")
    console.expect_exact("stdio_printf done failures=0")
