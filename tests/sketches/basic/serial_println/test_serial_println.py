"""The UART transmit path, and the gate for "a UART works" on every board.

Two streams: `dut` is Console, the harness (the debug module's data registers,
see tests/sketches/testcmd.h), and `uart` is the wire of the UART the runner
named before this script started ("UART <n> <route> <baud>"). The lines are read
off the wire; the verdict comes back on Console.

The banner is waited for rather than assumed, and nothing is asserted until the
board has been asked, so a pass cannot be the previous sketch's output arriving
late.
"""


def test_serial_println(dut, uart) -> None:
    dut.expect_exact("serial_println READY", timeout=20)
    dut.write("RUN\n")
    uart.expect_exact("hello from ch32")
    uart.expect_exact("int=42")
    # Print(value, HEX) is uppercase with no 0x prefix, as Arduino does.
    uart.expect_exact("hex=BEEF")
    dut.expect_exact("availableForWrite PASS")
    dut.expect_exact("serial_println done failures=0")
