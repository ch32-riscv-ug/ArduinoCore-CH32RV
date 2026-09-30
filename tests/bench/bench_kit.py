"""The harness protocol of tests/build/sketches/testcmd.h, for the bench tests (bench/*/*/test_*.py).

A bench test gets `dut` (the sketch's Console, read by ch32rv's monitor over the debug module) from
pytest-embedded-arduino-cli, `ch32_uart` (the DUT's UART on the probe) from pytest-embedded-arduino-cli-ch32rv,
and `bench` (tests/benches/<name>.toml, checked) from bench/conftest.py. What this module adds is the part only
this core's test sketches speak:

- start(dut, name): wait for "<name> READY" and prove the link with a PING carrying a fresh token, so a reply the
  previous sketch left behind cannot answer for this one.
- open_uart(dut, ch32_uart, bench): name the UART this bench wires to the probe ("UART <n> <route> <baud>", from
  the bench file's [uart]) and open the probe's side of it.

Loaded with `load("tests/bench/bench_kit.py", "bench_kit")` (tests/loader.py): a plain module, not a conftest.
"""
import uuid

BAUD = 115200


def start(dut, name: str) -> None:
    dut.expect_exact(f"{name} READY", timeout=20)
    token = uuid.uuid4().hex[:8]
    dut.write(f"PING {token}\n")
    dut.expect_exact(f"PONG {token}", timeout=10)


def open_uart(dut, ch32_uart, bench, baud: int = BAUD):
    try:
        n, route = bench.uart
    except Exception as e:                    # benchdef.BenchError: the jig wires no UART - not this DUT's fault
        import pytest
        pytest.skip(str(e))
    dut.write(f"UART {n} {route} {baud}\n")
    dut.expect_exact(f"UART OK {n} {route} {baud}", timeout=10)
    return ch32_uart.open(baud)
