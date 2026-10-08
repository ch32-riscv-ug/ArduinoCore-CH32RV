"""The UART transmit path, and the gate for "a UART works" on every board.

Two streams: `console` is Console, the harness (the debug module's data registers,
see tests-legacy/build/sketches/testcmd.h), and `uart` is the wire of the UART the runner
named before this script started ("UART <n> <route> <baud>"). The lines are read
off the wire; the verdict comes back on Console.

The banner is waited for rather than assumed, and nothing is asserted until the
board has been asked, so a pass cannot be the previous sketch's output arriving
late.

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench --profile <profile>
"""
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")


def test_serial_println(dut, ch32_uart, bench):
    kit.start(dut, 'serial_println')
    uart = kit.open_uart(dut, ch32_uart, bench)
    dut.write("RUN\n")
    uart.expect_exact("hello from ch32")
    uart.expect_exact("int=42")
    # Print(value, HEX) is uppercase with no 0x prefix, as Arduino does.
    uart.expect_exact("hex=BEEF")
    dut.expect_exact("availableForWrite PASS")
    dut.expect_exact("serial_println done failures=0")
