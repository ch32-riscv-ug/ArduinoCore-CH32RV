"""wire (case wire): Wire, the board's I2C (a hardware one, or a SoftWire when the fqbn sets it to Software); softwire
(case softwire): a SoftWire. tests/sketch_support/wire/wire.ino.

Two questions, kept apart (2026-11-14, the user): the clock - setClock() of 20 kHz to 3.4 MHz, the SCL on the wire
measured for the hardware Wire and for SoftWire alike where nothing but the DUT drives, and judged from what was
measured, no I2C mode chosen: never faster than asked (what the core promises), and within 10 % of it for clocks of at
most a fifth of the fastest SCL of the run (this part's and these lines' ceiling, measured here), which shows the
setting is right; how much the lines slow the rest is physics, not promised - and the transactions - repeated START, reads and writes against a target - run at 20 kHz, where the bench's
slow lines settle. The resident bench pulls the lines up with the probe's pads alone (P4, about 45 kOhm):
their rises take microseconds, beyond I2C's 1 us, and the probe's input reads high only from 0.75 VDD, above the
DUT's Schmitt inputs, so at 100 kHz a released SDA reads low at the probe while the DUT reads it high
(tests/README.ja.md 取り込みの速さ, TODO).

The lines are `<port>@<pad>` ones the probe labels (bench_lines.py gpio_lines): for Wire the two the DUT names as its
default SDA / SCL, which must be labelled; for SoftWire the first two lines that are not Wire's pins (Wire is begun on
those at start-up). Without them the test skips, naming what is missing.

wire.output-only runs first with no target: the probe only pulls the two lines up (its pads' input pull-ups) and
captures them, so what is seen is the DUT's own. At each clock, for two addresses the sketch writes
one byte with endTransmission(true): on the wire S, the address with no acknowledge, P (WireSkein's i2c), and
endTransmission() returns 2 (compared here) at 400 kHz and below - the I2C modes the reference manuals give;
above that the results are recorded (beyond_400k), not required. The SCL WireSkein measured is judged as above and
recorded with the run.

wire.repeated-start then makes the OEP probe's i2c-target (its own pull-ups) the target at 0x42, at 20 kHz: the sketch writes
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
# wire.output-only: the clocks asked, up to High-speed mode's 3.4 MHz. What a part and the bench's lines reach is
# measured, not assumed: the fastest SCL any of them gave is the ceiling the others are judged against.
SPEEDS_HZ = (20_000, 50_000, 100_000, 400_000, 1_000_000, 3_400_000)
# The reference manuals' I2C runs standard (100 kHz) and fast (400 kHz) mode: up to here every transfer must work.
# Above it a clock can be set (CCR down to 1) but whether a part and its lines run it is not known - the core takes it
# as asked (the user, 2026-11-15: an error only for what the hardware can never do) - so the results there are
# recorded, and only "never faster than asked" is judged on the clock that comes out.
PROMISED_HZ = 400_000
FASTER_REL = 0.03         # never faster than asked (the core's promise): the measurement's and the oscillator's error
NEAR_SHARE = 0.2          # clocks at most this share of the ceiling: the lines' rise is small against the period there,
NEAR_REL = 0.1            # so the clock comes out within this of what was asked - the setting is right
PROTOCOL_HZ = 20_000      # wire.repeated-start: the transactions, where the bench's slow lines settle
# tests/README.ja.md 取り込みの速さ: T = the shortest SCL half period asked, 147 ns at 3.4 MHz: 100 MS/s is 14.7 / T
# (the slower clocks get more; two lines stream up to 160 MS/s, oep-probe-arduino docs/guide/capture.ja.md). The i2c
# check's 50 ns spikes are 5 / T at this rate. WireSkein 0.0.28 reads I2C as a spec input (it drops pulses under
# 50 ns, reads a slow edge's many crossings as one, restores a clock the lines hid: notices slow_edge,
# clock_restored), so the bench's 10-190 ns bounces are no longer counted as clocks; captured without a glitch
# filter or hysteresis, as its docs/capturing section 2 advises for slow I2C.
RATE = 100_000_000


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
                for hz in SPEEDS_HZ:
                    ask(dut, f"clock {hz}", rb"CLOCK (\d+)")
                    for address in (ADDRESS, 0x13):
                        want = [i2c(scl, sda, transactions=[{"addr": address, "rw": "write", "bytes": [], "ack": False,
                                                             "start": "S", "end": "P"}]),
                                starts(idle), ends(idle)]
                        name = f"{hz // 1000} kHz, address 0x{address:02x}, no target"
                        with ws_run.section(2, name, expect=want):
                            ws_run.command(f"nack 0x{address:02x}")
                            dut.write(f"nack 0x{address:02x}\n")
                            m = wait_reply(dut, wait, rb"NACK (\d+)")
                            ws_run.reply(m.group(0).decode())
                            results[name] = int(m.group(1))
                            wait(0.02)

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=RATE, steps=steps)
    finally:
        for ch in channels:
            gpio.configure(ch, Gpio.INPUT)
        core.plan_release(host, [gpio.fn])
    record_property("results", json.dumps(results))
    record_property("beyond_400k", json.dumps({k: v for k, v in results.items()
                                               if int(k.split(" kHz")[0]) * 1000 > PROMISED_HZ}))
    bad = {k: v for k, v in results.items() if v != 2 and int(k.split(" kHz")[0]) * 1000 <= PROMISED_HZ}
    wrong = [f"endTransmission() gave {bad}, not 2 (address NACK)"] if bad else []
    wrong += judge_clocks(ws_run, record_property)
    assert not wrong, " | ".join(wrong)


def judge_clocks(ws_run, record_property):
    """The SCL WireSkein measured in each section (its i2c decode's scl_hz), judged here and recorded with the run
    (ws_run.result). What the core promises: never faster than asked - a hardware CCR rounded up, every SoftWire half
    at least the half asked - so no SCL above the clock asked. What it does not: how much the lines slow it (their
    rise, the bench's 45 k pull-ups) and SoftWire's ceiling (the CPU's). The fastest SCL of the run is taken as this
    part's and these lines' ceiling, measured here rather than known; clocks of at most a fifth of it must come out
    within 10 % of what was asked, which shows the setting is right."""
    from wireskein.verify import check_i2c
    got, where = {}, {}                             # hz -> [measured scl_hz], [section paths]
    for seg in ws_run.sections():
        name = seg["path"].rsplit("/", 1)[-1]
        if " kHz, address" not in name or seg["capture"] is None:
            continue
        hz = int(name.split(" kHz")[0]) * 1000
        pins = seg["path"].split("/")[0].split()[-2:]          # "... output only <sda> <scl>"
        _ok, measured, _why = check_i2c(seg["capture"], {"scl": pins[1], "sda": pins[0]})
        if measured.get("scl_hz"):
            got.setdefault(hz, []).append(measured["scl_hz"])
            where.setdefault(hz, []).append(seg["path"])
    if not got:
        return ["no SCL measured in any section"]
    ceiling = max(max(v) for v in got.values())
    record_property("scl_hz", json.dumps({str(hz): [round(x) for x in v] for hz, v in sorted(got.items())}))
    record_property("scl_ceiling_hz", round(ceiling))
    wrong = []
    for hz in sorted(got):
        for value, path in zip(got[hz], where[hz]):
            why = []
            if value > hz * (1 + FASTER_REL):
                why.append(f"{value:.0f} Hz is faster than the {hz} Hz asked")
            if hz <= ceiling * NEAR_SHARE and value < hz * (1 - NEAR_REL):
                why.append(f"{value:.0f} Hz is more than {NEAR_REL:.0%} under the {hz} Hz asked, at under "
                           f"{NEAR_SHARE:.0%} of the ceiling {ceiling:.0f} Hz")
            ws_run.result(path, "clock", "ng" if why else "ok",
                          measured={"scl_hz": round(value), "asked_hz": hz, "ceiling_hz": round(ceiling)},
                          reason="; ".join(why))
            wrong += [f"{hz // 1000} kHz: " + w for w in why]
    return wrong


def test_wire_repeated_start(dut, oep_host, ws_run, record_property):
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
        ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")

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
    It stays at 100 kHz, unlike the transactions above: the probe is the master here, and it passes on every series
    of the bench's lines (2026-11-14); the 20 kHz there is for the hardware Wire as the master (H41x's SDA hold
    against the probe's input, TODO)."""
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
        pytest.skip("n/a: this Wire has no target mode")
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
