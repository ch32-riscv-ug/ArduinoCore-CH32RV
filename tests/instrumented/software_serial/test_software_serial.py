"""software-serial: SoftwareSerial (the library) on two plain probe lines against the probe's UART - test_software_serial.
Split from the uart case (2026-11-19) with its sketch, sketch_support/software_serial/software_serial.ino, so that
uart.ino keeps CH32V003's 16 KB for Serial's own modes."""
import json
import pathlib
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import port_lines
import ws_capture

# (data bits, parity, stop bits) of the sketch's ssbegin formats.
FORMATS = {0: (8, "none", 1), 1: (8, "even", 1), 2: (8, "odd", 1), 3: (8, "none", 2), 4: (7, "even", 1), 5: (7, "odd", 1)}
COUNT = 16
SLOW_RATE = 4_000_000          # 115200: T 8.7 us, 35 / T


def _fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def _pattern(n, bits=8):
    return bytes(((i * 37 + 11) & 0xFF) & ((1 << bits) - 1) for i in range(n))


def _expect(dut, wait, pattern, timeout=3.0):
    """The DUT's reply, looked for in short tries with wait() between so the stream keeps being read."""
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def _ask(dut, wait, line, reply, timeout=3.0):
    dut.write(line + "\n")
    return _expect(dut, wait, reply, timeout)


def _ask_now(dut, line, reply, timeout=5.0):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def _oep_only(request):
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    return target, request.getfixturevalue("oep_host")


def _plain_lines(oep_host, target):
    """The port's `<port>@<pad>` lines (no role), not the chip's debug pads."""
    debug = set(target.get("debug_pads") or [])
    return [line for line in port_lines(oep_host, target) if line["role"] is None and line["pad"] not in debug]


def _plan_uart(oep_host, rx=None, tx=None):
    """A free oep.fixture.uart with RX on line `rx` and / or TX on line `tx`: (io, fn); None when none takes them."""
    from oep_client import core, fixture, host as oh, registry
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    plan = [(RX, rx["channel"])] if rx else []
    plan += [(TX, tx["channel"])] if tx else []
    for fn in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(fn, role, channel) for role, channel in plan])
        except oh.Rejected:
            continue
        return fixture.FixtureUartIO(host, fn), fn
    return None, None


SOFT_CONDITIONS = [(9600, 0), (57600, 0), (115200, 0), (9600, 1), (9600, 2), (9600, 3), (9600, 4)]
# SoftwareSerial receives in a pin interrupt and times bits by SysTick: how fast a part follows depends on its
# interrupt latency, not only its clock (V003 and V006 both at 24 MHz: V003 takes 115200, V006 7 bytes of 16 there,
# 2026-11-17). Like an I2C clock above 400 kHz (the user, 2026-11-15: what can be set but may not work is not an
# error), the receive above SOFT_PROMISED_BAUD is recorded, not required; what the DUT sends is judged at every speed.
SOFT_PROMISED_BAUD = 9600


def test_software_serial(dut, ws_run, request, record_property):
    """SoftwareSerial on the first two plain lines (RX, TX), against the probe's UART: 8N1 at 9600, 57600 and 115200,
    and 8E1, 8O1, 8N2, 7E1 at 9600. Each condition: the DUT sends 16 bytes (decoded on its TX at the nominal baud
    within 3 %, the format, no error) and receives 16 the probe sends (decoded on its RX; the DUT's count and FNV-1a,
    no overflow); the other line stays idle."""
    from oep_client import core, fixture
    from wireskein.runlog import ends, level, starts, uart
    target, oep_host = _oep_only(request)
    plain = _plain_lines(oep_host, target)
    if len(plain) < 2:
        pytest.skip("SoftwareSerial needs two plain <port>@<pad> lines")
    ss_rx, ss_tx = plain[0], plain[1]
    record_property("lines", f"rx {ss_rx['pad']} tx {ss_tx['pad']}")
    io, used = _plan_uart(oep_host, rx=ss_tx, tx=ss_rx)
    assert io is not None, "no free oep.fixture.uart takes the two lines"
    host = oep_host.host
    sent = _pattern(COUNT)
    got = {}

    def steps(wait):
        idle = {ss_tx["pad"]: 1, ss_rx["pad"]: 1}
        with ws_run.section(1, f"SoftwareSerial rx {ss_rx['pad']} tx {ss_tx['pad']}"):
            for baud, k in SOFT_CONDITIONS:
                bits, parity, stop = FORMATS[k]
                name = f"{baud} {bits}{parity[0].upper()}{stop}"
                mask = (1 << bits) - 1
                out = bytes(b & mask for b in sent)
                back = bytes(b & mask for b in reversed(sent))
                common = dict(bits=bits, parity=parity, stop=stop)
                # Begun before the section: until the first begin() the TX pad is no output yet.
                m = _ask(dut, wait, f"ssbegin {ss_rx['pin']} {ss_tx['pin']} {baud} {k}", rb"SSBEGIN (\d)")
                assert m.group(1) == b"1", f"{name}: SoftwareSerial is not listening"
                probe_baud = io.configure(baud, fixture.FixtureUart.format_byte(bits, parity[0].upper(), stop))
                wait(0.02)
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    ws_run.command(f"ssbegin {baud} {k}")
                    with ws_run.section(3, "dut sends", expect=[uart(ss_tx["pad"], baud, data=out.hex(), **common),
                                                                level(ss_rx["pad"], 1)]):
                        ws_run.command(f"sssend {COUNT}")
                        _ask(dut, wait, f"sssend {COUNT}", rb"SSSEND \d+", timeout=5)
                        wait(0.02)
                        io.read(4096)
                    with ws_run.section(3, "dut receives", expect=[uart(ss_rx["pad"], probe_baud, data=back.hex(),
                                                                        **common), level(ss_tx["pad"], 1)]):
                        ws_run.command(f"ssrecv {COUNT}")
                        dut.write(f"ssrecv {COUNT} {int(200 + 12000 * COUNT / baud)}\n")
                        _expect(dut, wait, rb"SSRECV READY")
                        io.write(back)
                        m = _expect(dut, wait, rb"SSRECV (\d+) ([0-9A-F]+) (\d)", timeout=5)
                        ws_run.reply(m.group(0).decode())
                        got[name] = (int(m.group(1)), m.group(2).decode(), m.group(3) == b"1")
                        wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [ss_rx, ss_tx], rate=SLOW_RATE, steps=steps)
    finally:
        core.plan_release(host, [used])
        _ask_now(dut, "ssend", rb"SSEND")
    record_property("received", json.dumps(got))
    wrong = []
    for baud, k in SOFT_CONDITIONS:
        bits, parity, stop = FORMATS[k]
        name = f"{baud} {bits}{parity[0].upper()}{stop}"
        back = bytes(b & ((1 << bits) - 1) for b in reversed(sent))
        if got.get(name) != (COUNT, f"{_fnv(back):X}", False) and baud <= SOFT_PROMISED_BAUD:
            wrong.append(f"{name}: {got.get(name)}")
    record_property("received_beyond", json.dumps({n: v for n, v in got.items()
                                                   if int(n.split()[0]) > SOFT_PROMISED_BAUD}))
    assert not wrong, "the DUT received: " + " | ".join(wrong)
