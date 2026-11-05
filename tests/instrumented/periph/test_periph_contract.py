"""periph (case periph): a second UART and an ADC input on `<port>@<pad>` lines the probe labels (bench_lines.py
gpio_lines), and the planned features on their role labels (tests/BENCH.ja.md §5).

- periph.uart2: Serial1 on a USART route both of whose pads are labelled (the variant's routes, place.py
  serial_routes; not the console's USART), at 115200 8N1. The probe's UART sends 200 random bytes and the sketch
  echoes them. Both lines stream in one capture (ws_capture.stream) with a WireSkein section: the bytes decoded on
  the DUT's RX and TX (uart: the format, F_CPU / BRR within 3 %, no framing error), both lines idle at its start
  and end; what came back to the probe is compared here.
- periph.adc: a labelled pad the DUT names as an ADC channel (not one a defect of the board's record takes): the probe
  drives it low, then high: analogRead() is under 5 % of full scale, then over 95 %.
- periph.can / i2s / argb / rs485 / qspi: not implemented in the core. With their lines labelled the test fails,
  saying so; that is the reminder that the bench is ready for them."""
import json
import pathlib
import secrets
import sys
import time

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import gpio_lines, port_lines, require
import ws_capture


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def ready(dut, case):
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"


def test_uart2(dut, oep_host, ws_run, record_property):
    from oep_client import core, fixture, host as oh, registry
    from wireskein.runlog import ends, starts, uart
    case = settings(__file__)
    target = case["target"]
    lines = {line["pad"]: line for line in gpio_lines(oep_host, target)}
    console = {line["pad"] for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    routes = target.get("serial_routes", [])
    console_usarts = {n for n, _, tx, rx in routes if {tx, rx} & console}
    pairs = [(n, k, tx, rx) for n, k, tx, rx in routes if n not in console_usarts and tx in lines and rx in lines]
    if not pairs:
        pytest.skip("no USART route besides the console's has both pads labelled <port>@<pad>")
    usart, route, tx, rx = pairs[0]
    record_property("route", f"USART{usart} route {route}: TX {tx}, RX {rx}")
    ready(dut, case)
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    io, used = None, None
    for fn in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(fn, RX, lines[tx]["channel"]), (fn, TX, lines[rx]["channel"])])
        except oh.Rejected:
            continue          # the console's, or busy
        io, used = fixture.FixtureUartIO(host, fn), fn
        break
    assert io is not None, "no free oep.fixture.uart on the probe for the second UART"
    payload = bytes([0, 0x55, 0xAA, 0xFF]) + secrets.token_bytes(196)
    f_cpu = int(target["f_cpu"])
    got = bytearray()

    def steps(wait):
        idle = {tx: 1, rx: 1}
        with ws_run.section(1, f"Serial1 USART{usart} route {route} TX {tx} RX {rx}"):
            with ws_run.section(2, "115200 8N1 echo", expect=[
                    uart(rx, probe_baud, data=payload.hex(), errors_max=0),
                    uart(tx, f_cpu / round(f_cpu / 115200), data=payload.hex(), errors_max=0),
                    starts(idle), ends(idle)]):
                ws_run.command(f"uart2 {lines[rx]['pin']} {lines[tx]['pin']} 115200")
                dut.write(f"uart2 {lines[rx]['pin']} {lines[tx]['pin']} 115200\n")
                m = dut.expect(rb"UART2 (\d)\r?\n", timeout=5)
                ws_run.reply(m.group(0).decode())
                assert m.group(1) == b"1", f"Serial1 did not start on TX {tx} / RX {rx}"
                wait(0.02)
                io.write(payload)
                deadline = time.monotonic() + 5
                while len(got) < len(payload) and time.monotonic() < deadline:
                    wait(0.02)
                    got.extend(io.read(480))
                wait(0.02)

    try:
        probe_baud = io.configure(115200)
        ws_capture.stream(ws_run, oep_host, [lines[tx], lines[rx]], rate=5_000_000, steps=steps)
        m = ask(dut, "stop", rb"STOP (\d+)")
        record_property("echoed", int(m.group(1)))
    finally:
        core.plan_release(host, [used])
    assert bytes(got[:len(payload)]) == payload, f"echo differs ({len(got)} of {len(payload)} bytes back)"


def test_adc(dut, oep_host, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    case = settings(__file__)
    lost = {int(f.split(":")[1]) for d in case["target"].get("record", {}).get("defects", [])
            for f in d.get("lose", []) if f.startswith("adc:")}
    ready(dut, case)
    line = None
    for candidate in gpio_lines(oep_host, case["target"]):
        channel = int(ask(dut, f"acaps {candidate['pin']}", rb"ACAPS (-?\d+)").group(1))
        if channel >= 0 and channel not in lost:
            line = candidate
            break
    if line is None:
        pytest.skip("no labelled <port>@<pad> line is an ADC channel" + (f" (the record's defects take {sorted(lost)})" if lost else ""))
    record_property("line", f"{line['pad']} channel {channel}")
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, line["channel"])])
    try:
        readings = {}
        for level, mode in ((0, Gpio.OUTPUT_LOW), (1, Gpio.OUTPUT_HIGH)):
            gpio.configure(line["channel"], mode)
            time.sleep(0.01)
            m = ask(dut, f"adc {line['pin']}", rb"ADC (\d+) (\d+)")
            readings[level] = (int(m.group(1)), int(m.group(2)))
        record_property("readings", json.dumps(readings))
        low, full = readings[0]
        high, _ = readings[1]
        assert low <= full * 0.05, f"{line['pad']} driven low reads {low} of {full}"
        assert high >= full * 0.95, f"{line['pad']} driven high reads {high} of {full}"
    finally:
        gpio.configure(line["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])


@pytest.mark.parametrize("feature", ["can", "i2s", "argb", "rs485", "qspi"])
def test_planned(dut, oep_host, feature):
    """Planned features: the names are fixed, the code is not written (FEATURE_CANDIDATES.ja.md)."""
    case = settings(__file__)
    lines = require(oep_host, case["target"], feature)
    pytest.fail(f"{feature}: the bench labels {', '.join(sorted(lines))}, but the core has no {feature} driver yet")
