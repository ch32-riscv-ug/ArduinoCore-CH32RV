"""Milestone 1: Serial.begin/print/println work on every target board.

The gate for "Serial works", and under the command protocol it is a two-way
gate: nothing is asserted until the board has been asked, so a pass cannot be
the previous sketch's output arriving late.

One test function, many checks - the board is asked once and every answer is
read in order. The banner is waited for rather than assumed: `dut` is opened
after the flashing tool has reset the board, so the sketch repeats
"serial_println READY" until it is asked (tests/sketches/testcmd.h).

    uv run pytest sketches/basic/serial_println --profile ch32v00x
"""


def test_serial_println(dut) -> None:
    dut.expect_exact("serial_println READY", timeout=20)
    dut.write("RUN\n")
    dut.expect_exact("hello from ch32")
    dut.expect_exact("int=42")
    # Print(value, HEX) is uppercase with no 0x prefix, as Arduino does.
    dut.expect_exact("hex=BEEF")
    dut.expect_exact("serial_println done failures=0")
