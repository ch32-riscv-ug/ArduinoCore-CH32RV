"""wire (case wire): Wire, the board's I2C (a hardware one, or a SoftWire when the fqbn sets it to Software); softwire
(case softwire): a SoftWire. tests/sketch_support/wire/wire.ino, at 100 kHz.

The lines are `<port>@<pad>` ones the probe labels (bench_lines.py gpio_lines): for Wire the two the DUT names as its
default SDA / SCL, which must be labelled; for SoftWire the first two lines that are not Wire's pins (Wire is begun on
those at start-up). Without them the test skips, naming what is missing.

wire.output-only runs first with no target: the probe only pulls the two lines up (its pads' input pull-ups) and
captures them, so what is seen is the DUT's own. For two addresses the sketch writes one byte with
endTransmission(true): on the wire S, the address with no acknowledge, P (WireSkein's i2c), and endTransmission()
returns 2 (compared here).

wire.repeated-start then makes the OEP probe's i2c-target (its own pull-ups) the target at 0x42: the sketch writes
0x10 with endTransmission(false), reads n bytes with requestFrom(addr, n, stop) and writes 0x20 with
endTransmission(true), back to back, for n = 1, 2 and 3 (the driver's three read paths) and stop 0 / 1. Arduino's
sendStop false keeps the bus, so on the wire it is S W Sr R Sr W P with stop 0 and S W Sr R P S W P with stop 1,
judged by WireSkein (i2c with each transaction's start and end, the bytes and the acknowledges, the bus released); the
sketch's results (0, 0) and the bytes it read are compared here.

Each test is one capture of SDA and SCL with a WireSkein section per condition, both lines high at its start and
end."""
import json
import pathlib
import secrets
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import gpio_lines
import ws_capture

from oep_client import core
from oep_client.fixture import Gpio, I2cTarget

ADDRESS = 0x42
RATE = 2_000_000          # 20 samples a bit at 100 kHz


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def pad(number):
    return f"P{'ABCDEFGHIJK'[(number >> 5) - 2]}{number & 31}"


def wait_reply(dut, wait, pattern, timeout=3.0):
    """The DUT's reply, looked for in short tries with wait() between so the stream keeps being read."""
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def open_bus(dut, oep_host, case, record_property):
    """Start the case's bus on the DUT and pick its lines: (lines by pad, sda, scl, soft)."""
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d+) (\d+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    wire_sda, wire_scl = pad(int(m.group(2))), pad(int(m.group(3)))
    soft = case.get("case") == "softwire"
    lines = {line["pad"]: line for line in gpio_lines(oep_host, case["target"])}
    if soft:
        free = [p for p in lines if p not in (wire_sda, wire_scl)]
        if len(free) < 2:
            pytest.skip(f"SoftWire wants two <port>@<pad> lines besides Wire's {wire_sda} / {wire_scl}")
        sda, scl = free[:2]
        assert ask(dut, f"soft {lines[sda]['pin']} {lines[scl]['pin']}", rb"SOFT (\d)").group(1) == b"1", \
            f"SoftWire refused SDA {sda} / SCL {scl}"
        record_property("bus", "SoftWire")
    else:
        sda, scl = wire_sda, wire_scl
        missing = [p for p in (sda, scl) if p not in lines]
        if missing:
            pytest.skip(f"Wire's pins {sda} / {scl}: the probe labels no " + ", ".join(f"<port>@{p.lower()}" for p in missing))
        record_property("bus", "Wire")
    record_property("pins", json.dumps({"sda": sda, "scl": scl}))
    return lines, sda, scl, soft


def test_wire_output_only(dut, oep_host, ws_run, record_property):
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    gpio = Gpio(host)
    channels = [lines[sda]["channel"], lines[scl]["channel"]]
    core.plan_apply(host, [(gpio.fn, 1, ch) for ch in channels])
    results = {}
    try:
        for ch in channels:
            gpio.configure(ch, Gpio.INPUT_PULLUP)     # the bus's pull-ups, with no target on it

        def steps(wait):
            idle = {sda: 1, scl: 1}
            with ws_run.section(1, f"{'softwire' if soft else 'wire'} output only {sda} {scl}"):
                with ws_run.section(2, "rest", expect=[starts(idle), ends(idle)]):
                    wait(0.02)
                for address in (ADDRESS, 0x13):
                    want = [i2c(scl, sda, transactions=[{"addr": address, "rw": "write", "bytes": [], "ack": False,
                                                         "start": "S", "end": "P"}]),
                            starts(idle), ends(idle)]
                    with ws_run.section(2, f"address 0x{address:02x}, no target", expect=want):
                        ws_run.command(f"nack 0x{address:02x}")
                        dut.write(f"nack 0x{address:02x}\n")
                        m = wait_reply(dut, wait, rb"NACK (\d+)")
                        ws_run.reply(m.group(0).decode())
                        results[address] = int(m.group(1))
                        wait(0.02)

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=RATE, steps=steps)
    finally:
        for ch in channels:
            gpio.configure(ch, Gpio.INPUT)
        core.plan_release(host, [gpio.fn])
    record_property("results", json.dumps(results))
    assert all(r == 2 for r in results.values()), f"endTransmission() gave {results}, not 2 (address NACK)"


def test_wire_contract(dut, oep_host, ws_run, record_property):
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    target = I2cTarget(host)
    core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    record_property("probe_pullups", target.internal_pullups)
    wrong = []
    try:
        target.configure(ADDRESS)

        def steps(wait):
            idle = {sda: 1, scl: 1}
            with ws_run.section(1, f"{'softwire' if soft else 'wire'} {sda} {scl} at 0x{ADDRESS:02x}"):
                for n in (1, 2, 3):
                    for stop in (0, 1):
                        reply = secrets.token_bytes(n)
                        target.preload_tx(reply)
                        if stop:
                            edges = [("S", "Sr"), ("Sr", "P"), ("S", "P")]
                        else:
                            edges = [("S", "Sr"), ("Sr", "Sr"), ("Sr", "P")]
                        transactions = [
                            {"addr": ADDRESS, "rw": "write", "bytes": [0x10], "ack": True},
                            {"addr": ADDRESS, "rw": "read", "bytes": list(reply), "ack": True},
                            {"addr": ADDRESS, "rw": "write", "bytes": [0x20], "ack": True},
                        ]
                        for t, (start, end) in zip(transactions, edges):
                            t.update(start=start, end=end)
                        want = [i2c(scl, sda, transactions=transactions), starts(idle), ends(idle)]
                        name = f"n {n} stop {stop}"
                        with ws_run.section(2, name, expect=want):
                            command = f"seq {ADDRESS} {n} {stop}"
                            ws_run.command(command)
                            dut.write(command + "\n")
                            m = wait_reply(dut, wait, rb"SEQ (\d+) (\d+) (\S+) (\d+)")
                            ws_run.reply(m.group(0).decode())
                            first, got, hexed, last = int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4))
                            if (first, last) != (0, 0) or got != n or hexed.lower() != reply.hex():
                                wrong.append(f"{name}: endTransmission {first} / {last}, read {got} bytes {hexed}, "
                                             f"the target sent {reply.hex()}")
                            while target.read_rx()[0]:     # what the target took in: the wire is WireSkein's
                                pass
                            wait(0.01)

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)
