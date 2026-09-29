"""The harness protocol of tests/sketches/testcmd.h, for the bench tests (sketches/*/*/bench_*.py).

A bench test gets `dut` (the sketch's Console, read by ch32rv's monitor over the debug module) from
pytest-embedded-arduino-cli and `ch32_uart` (the DUT's UART on the probe) from pytest-embedded-arduino-cli-ch32rv.
What this module adds is the part only this core's test sketches speak:

- start(dut, name): wait for "<name> READY" and prove the link with a PING carrying a fresh token, so a reply the
  previous sketch left behind cannot answer for this one.
- open_uart(dut, ch32_uart, profile): name the UART this bench wires to the probe ("UART <n> <route> <baud>",
  from TEST_UART_<PROFILE>=<n>,<route> in .env) and open the probe's side of it.

Loaded with `load("tests/sketches/bench_kit.py", "bench_kit")` (tests/loader.py): a plain module, not a conftest.
"""
import os
import uuid

import pytest

BAUD = 115200


def start(dut, name: str) -> None:
    dut.expect_exact(f"{name} READY", timeout=20)
    token = uuid.uuid4().hex[:8]
    dut.write(f"PING {token}\n")
    dut.expect_exact(f"PONG {token}", timeout=10)


def uart_wiring(profile: str) -> tuple[int, int]:
    """Which USART and route of this board reach the probe: TEST_UART_<PROFILE>=<n>,<route>."""
    key = f"TEST_UART_{profile.upper().replace('-', '_')}"
    value = os.environ.get(key)
    if not value:
        pytest.fail(f"{key} is not set: which USART and route of this board reach the probe (tests/.env.example)")
    n, route = (int(v) for v in value.split(","))
    return n, route


def open_uart(dut, ch32_uart, profile: str, baud: int = BAUD):
    n, route = uart_wiring(profile)
    dut.write(f"UART {n} {route} {baud}\n")
    dut.expect_exact(f"UART OK {n} {route} {baud}", timeout=10)
    return ch32_uart.open(baud)
