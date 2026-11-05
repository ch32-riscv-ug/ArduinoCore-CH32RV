"""uart.external-round-trip: default USART route, 115200 8N1, binary 64-byte payload; then stdio on
the same port: puts() reaches Serial once it is begun, ch32rv_set_stdout(nullptr) discards, and
ch32rv_set_stdout(&Serial) brings it back.

uart.output-only (OEP ports with <port>.rx@<pad>): the formats' conditions with no UART on the probe, the DUT's TX
only, decoded the same way - the baseline. uart.formats (OEP ports with <port>.rx@<pad> and <port>.tx@<pad> labelled): 9600, 115200 and 1 Mbaud at 8N1, and
8E1, 8O1, 8N2, 7E1, 7O1 at 115200, both ways, in one capture of the two lines with a WireSkein section per
condition (both lines idle at its start and end): the DUT sends 16 bytes (decoded on its TX: the bytes, the format, the bit time F_CPU / BRR within 3 %,
no framing or parity error) and receives 16 the probe's UART sends (decoded on its RX the same way, and the DUT's
count and FNV-1a hash of what it read); the other line stays idle meanwhile."""
import secrets


def test_uart_contract(dut, ch32_uart):
    uart = ch32_uart.open(115200)
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(60)
    dut.write("uart\n")
    dut.expect_exact("UART READY", timeout=15)
    uart.write(payload)
    uart.expect_exact(payload, timeout=10)
    dut.expect_exact("UART COUNT 64", timeout=10)
    dut.write("stdio\n")
    dut.expect_exact("STDIO DONE", timeout=10)
    m = uart.expect(rb"(?s)STDIO SERIAL\r?\n(.*?)STDIO BACK", timeout=10)
    assert b"DISCARDED" not in m.group(1), m.group(1)


# ---- uart.formats: every baud and format the condition list names, both directions, judged by WireSkein ----------
import json
import pathlib
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import port_lines
import ws_capture

# (baud, format index in the sketch's ucfg, data bits, parity, stop bits): the driver's branches - BRR at a low, the
# usual and a high rate, then parity (the ninth bit with 8 data bits), two stop bits and 7 data bits at 115200.
FORMATS = {0: (8, "none", 1), 1: (8, "even", 1), 2: (8, "odd", 1), 3: (8, "none", 2), 4: (7, "even", 1), 5: (7, "odd", 1)}
CONDITIONS = [(9600, 0), (115200, 0), (1000000, 0), (115200, 1), (115200, 2), (115200, 3), (115200, 4), (115200, 5)]
COUNT = 16


def _fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def test_uart_output_only(dut, ws_run, request, record_property):
    """Every condition of uart.formats with no UART on the probe: it only captures the DUT's TX, so what is seen is
    the DUT's own (the baseline when the round trip looks wrong). The DUT sends 16 bytes in each; decoded on its TX
    (the bytes, the format, the bit time F_CPU / BRR within 3 %, no framing or parity error), idle at the section's
    start and end."""
    from wireskein.runlog import ends, starts, uart
    case = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    target = case["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("the formats need the probe's logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    roles = {line["role"]: line for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    if "rx" not in roles:
        pytest.skip("the probe labels no <port>.rx@<pad> (the DUT's TX)")
    dut_tx = roles["rx"]
    f_cpu = int(target["f_cpu"])
    sent = bytes((i * 37 + 11) & 0xFF for i in range(COUNT))

    def steps(wait):
        idle = {dut_tx["pad"]: 1}
        with ws_run.section(1, f"uart output only {dut_tx['pad']}"):
            for baud, k in CONDITIONS:
                bits, parity, stop = FORMATS[k]
                name = f"{baud} {bits}{parity[0].upper()}{stop}"
                out = bytes(b & ((1 << bits) - 1) for b in sent)
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ucfg {baud} {k}")
                    dut.write(f"ucfg {baud} {k}\n")
                    _expect(dut, wait, rb"UCFG 1")
                    wait(0.02)
                    with ws_run.section(3, "dut sends", expect=[uart(dut_tx["pad"], f_cpu / round(f_cpu / baud),
                                                                     data=out.hex(), bits=bits, parity=parity,
                                                                     stop=stop, errors_max=0)]):
                        ws_run.command(f"usend {COUNT}")
                        dut.write(f"usend {COUNT}\n")
                        _expect(dut, wait, rb"USEND \d+")
                        wait(0.02 + 12 * COUNT / baud)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx], rate=5_000_000, steps=steps)
    finally:
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d\r?\n", timeout=5)


def _expect(dut, wait, pattern, timeout=3.0):
    """The DUT's reply, looked for in short tries with wait() between so the stream keeps being read."""
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def test_uart_formats(dut, ws_run, request, record_property):
    from oep_client import core, fixture, host as oh, registry
    case = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())
    target = case["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("the formats need the probe's logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    roles = {line["role"]: line for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    if set(roles) != {"rx", "tx"}:
        pytest.skip("the probe labels no <port>.rx@<pad> and <port>.tx@<pad>")
    dut_tx, dut_rx = roles["rx"], roles["tx"]          # the probe's rx hears the DUT's TX
    f_cpu = int(target["f_cpu"])
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    io, used = None, None
    for fn in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(fn, RX, dut_tx["channel"]), (fn, TX, dut_rx["channel"])])
        except oh.Rejected:
            continue
        io, used = fixture.FixtureUartIO(host, fn), fn
        break
    assert io is not None, "no free oep.fixture.uart on the probe"
    sent = bytes((i * 37 + 11) & 0xFF for i in range(COUNT))
    hashes = {}

    def ask(wait, line, reply, timeout=3.0):
        dut.write(line + "\n")
        end = time.monotonic() + timeout
        while True:
            try:
                return dut.expect(reply + rb"\r?\n", timeout=0.05)
            except pexpect.TIMEOUT:
                if time.monotonic() > end:
                    raise
                wait(0.05)

    def steps(wait):
        from wireskein.runlog import ends, level, only_moving, starts, uart
        with ws_run.section(1, f"uart {dut_tx['pad']} / {dut_rx['pad']}", expect=[only_moving([dut_tx["pad"], dut_rx["pad"]])]):
            for baud, k in CONDITIONS:
                bits, parity, stop = FORMATS[k]
                name = f"{baud} {bits}{parity[0].upper()}{stop}"
                mask = (1 << bits) - 1
                dut_baud = f_cpu / round(f_cpu / baud)         # what BRR makes of it
                out = bytes(b & mask for b in sent)
                back = bytes(b & mask for b in reversed(sent))
                idle = {dut_tx["pad"]: 1, dut_rx["pad"]: 1}
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ucfg {baud} {k}")
                    assert ask(wait, f"ucfg {baud} {k}", rb"UCFG (\d)").group(1) == b"1", f"{name}: Serial.begin refused"
                    probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(bits, parity[0].upper(), stop))
                    wait(0.02)
                    common = dict(bits=bits, parity=parity, stop=stop, errors_max=0)
                    with ws_run.section(3, "dut sends", expect=[uart(dut_tx["pad"], dut_baud, data=out.hex(), **common),
                                                                level(dut_rx["pad"], 1)]):
                        ws_run.command(f"usend {COUNT}")
                        ask(wait, f"usend {COUNT}", rb"USEND \d+")
                        wait(0.02 + 12 * COUNT / baud)
                        io.read(4096)                           # what the probe's UART took in, not judged here
                    with ws_run.section(3, "dut receives",
                                        expect=[uart(dut_rx["pad"], probe_baud, data=back.hex(), **common),
                                                level(dut_tx["pad"], 1)]):
                        ws_run.command(f"urecv {COUNT}")
                        dut.write(f"urecv {COUNT} {int(200 + 12000 * COUNT / baud)}\n")
                        dut.expect(rb"URECV READY\r?\n", timeout=3)
                        io.write(back)
                        m = None
                        end = time.monotonic() + 3
                        while m is None:
                            try:
                                m = dut.expect(rb"URECV (\d+) ([0-9A-F]+)\r?\n", timeout=0.05)
                            except pexpect.TIMEOUT:
                                assert time.monotonic() < end, f"{name}: no URECV"
                                wait(0.05)
                        ws_run.reply(m.group(0).decode())
                        hashes[name] = (int(m.group(1)), m.group(2).decode())
                        wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [dut_tx, dut_rx], rate=5_000_000, steps=steps)
    finally:
        core.plan_release(host, [used])
        dut.write("ucfg 115200 0\n")
        dut.expect(rb"UCFG \d\r?\n", timeout=5)
    record_property("received", json.dumps(hashes))
    for baud, k in CONDITIONS:
        bits, parity, stop = FORMATS[k]
        name = f"{baud} {bits}{parity[0].upper()}{stop}"
        back = bytes(b & ((1 << bits) - 1) for b in reversed(sent))
        assert hashes.get(name) == (COUNT, f"{_fnv(back):X}"), f"{name}: the DUT received {hashes.get(name)}"
