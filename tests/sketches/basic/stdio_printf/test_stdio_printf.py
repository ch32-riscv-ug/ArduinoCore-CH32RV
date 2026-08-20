"""printf()/puts()/write() reach the serial monitor.

Both bugs this guards against were silent: the sketch either reset before
printing anything (libgloss's semihosting _write) or printed nothing while
returning success (the printf bridge compiled to `return 0`). So the tests
check the *output arrived*, not just that the call returned.

    uv run pytest sketches/basic/stdio_printf --profile ch32x035 --port /dev/ttyACM4
"""


def test_reaches_setup(dut) -> None:
    """Printed before any stdio call: silence here means the board reset."""
    dut.expect_exact("stdio test start")


def test_raw_write(dut) -> None:
    """write(2) must go to the UART, not to a semihosting ecall."""
    dut.expect_exact("write=direct")
    dut.expect_exact("write returned 14")


def test_printf(dut) -> None:
    dut.expect_exact("printf=42 str x")


def test_printf_returns_length(dut) -> None:
    """-1 was the symptom of the bridge returning 0 for every byte."""
    dut.expect_exact("printf returned 17")


def test_puts(dut) -> None:
    dut.expect_exact("puts=line")
    dut.expect_exact("puts returned ok")


def test_buffered_conversion(dut) -> None:
    dut.expect_exact("wide=deadbeef")


def test_completes(dut) -> None:
    dut.expect_exact("stdio test done")
