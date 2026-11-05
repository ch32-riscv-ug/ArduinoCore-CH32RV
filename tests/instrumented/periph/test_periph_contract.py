"""periph (case periph): a second UART and an ADC input on `<port>@<pad>` lines the probe labels (bench_lines.py
gpio_lines), and the planned features on their role labels (tests/BENCH.ja.md §5).

- periph.serial-routes: Serial1 begun on each pair of pads that is a USART route (the variant's routes, place.py
  serial_routes; not the console's USART) with both pads labelled, one after another in one capture of all their
  lines (ws_capture.stream), a WireSkein section per pair: at 115200 8N1 the probe's UART sends 64 bytes (random
  after 00 55 AA FF) and the sketch echoes them; the bytes decoded on the DUT's RX and TX (uart: the format, F_CPU /
  BRR within 3 %, no framing error), only that pair moving (a remap that lands elsewhere moves another line), every
  line idle at its start and end (the probe's input pull-ups hold the lines the section does not use). Serial1 must take a USART the pair is a route of (peripheral()), and what came
  back to the probe is compared here.
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


def test_serial_routes(dut, oep_host, ws_run, record_property):
    from oep_client import core, fixture, host as oh, registry
    from oep_client.fixture import Gpio
    from wireskein.runlog import ends, only_moving, starts, uart
    case = settings(__file__)
    target = case["target"]
    lines = {line["pad"]: line for line in gpio_lines(oep_host, target)}
    console = {line["pad"] for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    routes = target.get("serial_routes", [])
    console_usarts = {n for n, _, tx, rx in routes if {tx, rx} & console}
    pairs = {}                      # (tx, rx): [(usart, route), ...] - one section per pad pair
    for n, k, tx, rx in routes:
        if n not in console_usarts and tx in lines and rx in lines:
            pairs.setdefault((tx, rx), []).append((n, k))
    if not pairs:
        pytest.skip("no USART route besides the console's has both pads labelled <port>@<pad>")
    record_property("routes", json.dumps({f"{tx}/{rx}": [f"USART{n} route {k}" for n, k in nk]
                                          for (tx, rx), nk in pairs.items()}))
    ready(dut, case)
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    uarts = core.find_all(host, "oep.fixture.uart")
    f_cpu = int(target["f_cpu"])
    pads = sorted({p for pair in pairs for p in pair})
    wrong = []

    gpio = Gpio(host)

    def probe_uart(tx, rx):
        """The probe's UART on this pair (the console's is busy), and its pads' input pull-ups on every other line
        (released or never driven, they would float): (io, fn)."""
        core.plan_release(host, [gpio.fn])
        others = [lines[p]["channel"] for p in pads if p not in (tx, rx)]
        if others:
            core.plan_apply(host, [(gpio.fn, 1, ch) for ch in others])
            for ch in others:
                gpio.configure(ch, Gpio.INPUT_PULLUP)
        for fn in uarts:
            try:
                core.plan_apply(host, [(fn, RX, lines[tx]["channel"]), (fn, TX, lines[rx]["channel"])])
            except oh.Rejected:
                continue
            return fixture.FixtureUartIO(host, fn), fn
        raise AssertionError("no free oep.fixture.uart on the probe for Serial1")

    def steps(wait):
        idle = {p: 1 for p in pads}
        with ws_run.section(1, f"Serial1 routes {' '.join(f'{tx}/{rx}' for tx, rx in pairs)}"):
            for (tx, rx), usarts in pairs.items():
                payload = bytes([0, 0x55, 0xAA, 0xFF]) + secrets.token_bytes(60)
                io, used = probe_uart(tx, rx)
                try:
                    probe_baud = io.configure(115200)
                    name = " / ".join(f"USART{n} route {k}" for n, k in usarts) + f" TX {tx} RX {rx}"
                    with ws_run.section(2, name, expect=[
                            uart(rx, probe_baud, data=payload.hex(), errors_max=0),
                            uart(tx, f_cpu / round(f_cpu / 115200), data=payload.hex(), errors_max=0),
                            only_moving([tx, rx]), starts(idle), ends(idle)]):
                        command = f"uart2 {lines[rx]['pin']} {lines[tx]['pin']} 115200"
                        ws_run.command(command)
                        dut.write(command + "\n")
                        m = dut.expect(rb"UART2 (\d) (\d+)\r?\n", timeout=5)
                        ws_run.reply(m.group(0).decode())
                        took = int(m.group(2))
                        if m.group(1) != b"1" or took not in {n for n, _ in usarts}:
                            wrong.append(f"{name}: Serial1 began {m.group(1).decode()} on USART{took}")
                        wait(0.02)
                        io.write(payload)
                        got = bytearray()
                        deadline = time.monotonic() + 3
                        while len(got) < len(payload) and time.monotonic() < deadline:
                            wait(0.02)
                            got.extend(io.read(480))
                        m = ask(dut, "stop", rb"STOP (\d+)")
                        ws_run.reply(m.group(0).decode())
                        if bytes(got[:len(payload)]) != payload:
                            wrong.append(f"{name}: echo differs ({len(got)} of {len(payload)} bytes back)")
                        wait(0.02)
                finally:
                    core.plan_release(host, [used])

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in pads], rate=5_000_000, steps=steps)
    finally:
        for p in pads:
            try:
                gpio.configure(lines[p]["channel"], Gpio.INPUT)
            except Exception:
                pass            # not this fn's channel any more
        core.plan_release(host, [gpio.fn])
    assert not wrong, " | ".join(wrong)


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
