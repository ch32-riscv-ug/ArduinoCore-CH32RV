"""serial_println on a bench board: the UART transmit path, through pytest-embedded-arduino-cli.

Not named test_*: a bench test needs a board behind a port, so the plain `pytest` run (CI) must not collect it.
Run it with the bench's .env (ports and UART wiring per profile, see tests/.env.example):

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest -o python_files='bench_*.py' sketches/basic --profile ch32v203

`dut` is the harness Console (SerialDMSeq, the profile's port_config source=dmseq); `ch32_uart` is the DUT's UART as
the probe sees it (a WCH-Link's UART bridge, or an OEP probe's fixture UART).
"""
import os
import uuid

import pytest

BAUD = 115200


def uart_wiring(profile: str) -> tuple[int, int]:
    """Which USART and route this bench wires to the probe for the board: TEST_UART_<PROFILE>=<n>,<route>."""
    key = f"TEST_UART_{profile.upper().replace('-', '_')}"
    value = os.environ.get(key)
    if not value:
        pytest.fail(f"{key} is not set: which USART and route of this board reach the probe (tests/.env.example)")
    n, route = (int(v) for v in value.split(","))
    return n, route


def test_serial_println(dut, ch32_uart, arduino_cli_app):
    n, route = uart_wiring(arduino_cli_app.profile)
    dut.expect_exact("serial_println READY", timeout=20)

    # A PONG carrying our own token: a reply the previous sketch left behind cannot answer for this one.
    token = uuid.uuid4().hex[:8]
    dut.write(f"PING {token}\n")
    dut.expect_exact(f"PONG {token}", timeout=10)

    dut.write(f"UART {n} {route} {BAUD}\n")
    dut.expect_exact(f"UART OK {n} {route} {BAUD}", timeout=10)
    uart = ch32_uart.open(BAUD)

    dut.write("RUN\n")
    uart.expect_exact("hello from ch32", timeout=10)
    uart.expect_exact("int=42")
    uart.expect_exact("hex=BEEF")
    dut.expect_exact("availableForWrite PASS", timeout=10)
    dut.expect_exact("serial_println done failures=0", timeout=10)
