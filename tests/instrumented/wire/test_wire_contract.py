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


SLAVE_ADDRESS = 0x42


def test_wire_slave(dut, oep_host, ws_run, record_property):
    """wire.slave (case wire only): Wire.begin(0x42) on its default pins with onReceive / onRequest, the probe's
    i2c-controller the master at 100 kHz (its pull-ups). One capture of SDA and SCL, a WireSkein section per
    condition judging the bus (i2c: the transactions, their starts and ends, the bytes, the bus released); what the
    sketch's callbacks saw (sgot) and what the probe read are compared here - Wire's target contract
    (libraries/Wire/README.md, AVR's semantics):
    - write: 3 bytes - onReceive(3) after the stop, those bytes;
    - read: onRequest() writes 4 bytes, the master reads 4;
    - register read: write 1 byte, repeated START, read 2 - onReceive first, then onRequest (R then Q);
    - read past the reply: 2 bytes provided, 4 read - the rest 0xFF;
    - long write: 40 bytes - onReceive gets the buffer's 32 (CH32RV_WIRE_BUFFER_SIZE), the head;
    - another address: 0x43 - not acknowledged, no callback.
    Prepared before a bench target had a usable hardware I2C (the x035 bench's lot has none, 2026-11-05)."""
    from oep_client.fixture import I2cController, I2cSegment
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    if case.get("case") != "wire":
        pytest.skip("the target mode is the hardware Wire's")
    lines, sda, scl, _ = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    ctrl = I2cController(host)
    core.plan_apply(host, ctrl.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    record_property("probe_pullups", ctrl.internal_pullups)
    m = ask(dut, f"slave {SLAVE_ADDRESS}", rb"SLAVE (\d)")
    if m.group(1) != b"1":
        pytest.skip("this Wire has no target mode")
    clock = ctrl.configure(100_000, 25_000)
    wrong = []

    def got(wait):
        dut.write("sgot\n")
        m = wait_reply(dut, wait, rb"SGOT (\d+) (\d+) (\S+) (-?\d+) (\S+)")
        ws_run.reply(m.group(0).decode())
        return int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4)), m.group(5).decode()

    def steps(wait):
        idle = {sda: 1, scl: 1}
        A = SLAVE_ADDRESS
        with ws_run.section(1, f"wire slave {sda} {scl} at 0x{A:02x}, master {clock} Hz"):
            def run(name, segments, transactions, reply=None):
                with ws_run.section(2, name, expect=[i2c(scl, sda, transactions=transactions),
                                                     starts(idle), ends(idle)]):
                    if reply is not None:
                        dut.write(f"sreply {reply.hex() or '-'}\n")
                        wait_reply(dut, wait, rb"SREPLY \d+")
                    ws_run.command(name)
                    done = ctrl.transfer(segments)
                    wait(0.02)
                    r = got(wait)
                    wait(0.01)
                    return done, r

            data = secrets.token_bytes(3)
            done, (rx, rq, order, count, hexed) = run("write 3", [I2cSegment.write(A, data)],
                [{"addr": A, "rw": "write", "bytes": list(data), "ack": True, "start": "S", "end": "P"}])
            if (rx, rq, count, hexed) != (1, 0, 3, data.hex()) or not done.ok:
                wrong.append(f"write 3: result {done.result}, onReceive {rx} onRequest {rq}, {count} bytes {hexed}")
            reply = secrets.token_bytes(4)
            done, (rx, rq, order, count, hexed) = run("read 4", [I2cSegment.reading(A, 4)],
                [{"addr": A, "rw": "read", "bytes": list(reply), "ack": True, "start": "S", "end": "P"}], reply)
            if (rx, rq) != (1, 1) or done.data != reply:
                wrong.append(f"read 4: onReceive {rx} onRequest {rq}, the master read {done.data.hex()}")
            reply = secrets.token_bytes(2)
            done, (rx, rq, order, count, hexed) = run("register read",
                [I2cSegment.write(A, b"\x05"), I2cSegment.reading(A, 2)],
                [{"addr": A, "rw": "write", "bytes": [5], "ack": True, "start": "S", "end": "Sr"},
                 {"addr": A, "rw": "read", "bytes": list(reply), "ack": True, "start": "Sr", "end": "P"}], reply)
            if (rx, rq, count, hexed) != (2, 2, 1, "05") or not order.endswith("RQ") or done.data != reply:
                wrong.append(f"register read: onReceive {rx} onRequest {rq} order {order}, {count} bytes {hexed}, "
                             f"the master read {done.data.hex()}")
            reply = secrets.token_bytes(2)
            done, (rx, rq, order, count, hexed) = run("read past the reply", [I2cSegment.reading(A, 4)],
                [{"addr": A, "rw": "read", "bytes": list(reply + b"\xff\xff"), "ack": True, "start": "S", "end": "P"}],
                reply)
            if done.data != reply + b"\xff\xff":
                wrong.append(f"read past the reply: the master read {done.data.hex()}, want {(reply + b'  ').hex()[:4]}ffff")
            data = secrets.token_bytes(40)
            done, (rx, rq, order, count, hexed) = run("long write", [I2cSegment.write(A, data)],
                [{"addr": A, "rw": "write", "bytes": list(data), "start": "S", "end": "P"}])
            if (count, hexed) != (32, data[:32].hex()):
                wrong.append(f"long write: onReceive({count}) {hexed}, want the head 32")
            before = got(wait)[:2]
            done, after = run("another address", [I2cSegment.write(A + 1, b"\x01")],
                [{"addr": A + 1, "rw": "write", "bytes": [], "ack": False, "start": "S", "end": "P"}])
            if done.result != I2cController.ADDRESS_NACK or after[:2] != before:
                wrong.append(f"another address: result {done.result}, callbacks {before} -> {after[:2]}")

    try:
        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [ctrl.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)
