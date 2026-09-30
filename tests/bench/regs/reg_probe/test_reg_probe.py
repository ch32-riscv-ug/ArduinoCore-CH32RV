"""The debugger as the judge (TEST_PLAN's method 3). The host tells the sketch which core API to call, reads the
peripheral registers through the probe's riscv-dm, and compares them with what ch32-device-data says - the core's
own register map is the thing under test, so the expectations never come from it. No wiring beyond the bench's.

The sketch's console is the bench's UART, not the debug module: reading memory needs the core halted, and a halt /
resume leaves the debug-module console silent for seconds. That UART is then the "monitor" - the SERIAL scenarios
skip it, and the excursion moves it and lets it come back.

One test: every scenario runs (sanity, clock, gpio, serial, exti, pwm, adc, tone, wire, spi), the report is
printed per group, and the test fails with every mismatched register named at once.

  uv run --env-file .env pytest bench/regs/reg_probe --profile ch32x035 -s
"""
import pytest
from loader import find_tables, load

regcheck = load("tests/bench/regs/reg_probe/regcheck.py", "regcheck")


def test_registers_hold_what_device_data_says(request, dut, bench, ch32_uart):
    if bench.probe.get("kind") != "oep":
        pytest.skip("the register reads go through an OEP probe's riscv-dm")
    if not find_tables():
        pytest.skip("ch32-device-data is not in <repo>/.tools: uv run tools/index/fetch_tools.py --tool ch32-device-data")
    import pexpect
    try:
        dut.expect(pexpect.TIMEOUT, timeout=1.5)       # let the monitor open its session: the broker is its
    except Exception:   # noqa: BLE001
        pass
    console = ch32_uart.open(115200)
    console.expect_exact("reg_probe READY", timeout=20)
    oep_host = request.getfixturevalue("oep_host")
    from oep_client import target
    wire = target.Wire(oep_host.host, "oep.wire." + bench.data["slot"]["wire"])
    conn, _ = wire.attach(halt=False)
    dm = target.RiscvDm(oep_host.host, conn)
    board = bench.data["dut"]["board"]
    part = bench.data["dut"].get("part")
    usart, _route = bench.uart
    pads = {p for p in (bench.data["uart"].get("tx"), bench.data["uart"].get("rx")) if p}
    import os
    only = os.environ.get("TEST_REGS_ONLY", "").split(",") if os.environ.get("TEST_REGS_ONLY") else None   # debugging aid
    try:
        result = regcheck.run(console, dm, board, part, log=print, only=only, monitor=usart, monitor_pads=pads)
    except regcheck.Failure as e:
        pytest.fail(str(e), pytrace=False)
    finally:
        print("--- console, last 1500 bytes ---")
        print(bytes(console.log[-1500:]).decode(errors="replace"))
    print(regcheck.summary(result))
    failed = result["report"].failed()
    assert not failed, "\n".join(c.describe() for c in failed)
