"""The UART receive path: the host talks on the wire, the board answers on it.

serial_println only proves the transmit path. This drives the other direction
over the UART the runner named ("UART <n> <route> <baud>"), so the board's RX
pin has to reach the runner's UART TX as well. `console` is Console, the harness;
`uart` is that wire (tests/sketches/testcmd.h).

The one sketch with no RUN - receiving *is* what is under test, so its
vocabulary is the test (see tests/TEST_PLAN.ja.md).

Bench test (not test_*: it needs a board behind a port). Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest -o python_files='bench_*.py' sketches/basic --profile <profile>
"""
from loader import load

kit = load("tests/sketches/bench_kit.py", "bench_kit")


def test_serial_echo(dut, ch32_uart, arduino_cli_app):
    kit.start(dut, 'serial_echo')
    uart = kit.open_uart(dut, ch32_uart, arduino_cli_app.profile)

    # A line sent to the target comes back with the echo prefix.
    uart.write("ECHO hello\n")
    uart.expect_exact("echo:hello")

    # The target parsed the argument rather than reflecting the bytes.
    uart.write("LEN abcdef\n")
    uart.expect_exact("len=6")

    # Ten lines back to back: the RX ring keeps up and stays aligned.
    for i in range(10):
        uart.write(f"ECHO line{i}\n")
        uart.expect_exact(f"echo:line{i}")

    # Never silence: a host that is out of step has to find out at once.
    uart.write("NOSUCHCOMMAND\n")
    uart.expect_exact("unknown:NOSUCHCOMMAND")
