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
# The other cases' clocks are 100 kHz at most (PROTOCOL_HZ, SOFT_SLAVE_HZ, SOFT_EXT_HZ): 40 MS/s is 200 / T there
# and still two samples of a 50 ns spike, at 10 MB/s for two lines - 2.5 times the probe's room of 100 MS/s, whose
# buffers last only a few hundred ms when the reader stalls ([oep], 2026-11-18).
PROTOCOL_RATE = 40_000_000


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line)
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
        # SoftWire's target takes an interrupt on each line: SCL on a pin bit (EXTI line) other than SDA's
        sda = free[0]
        scl = next((p for p in free[1:] if int(p[2:]) != int(sda[2:])), free[1])
        assert ask(dut, f"soft {lines[sda]['pin']} {lines[scl]['pin']}", rb"SOFT (\d)").group(1) == b"1", \
            f"SoftWire refused SDA {sda} / SCL {scl}"
        record_property("bus", "SoftWire")
    else:
        # The board's record: a defect that takes the hardware I2C's pads (a pull-down on them, x035-usb-pads,
        # l103-evt-cc-rd) leaves Wire untestable there; SoftWire runs on other lines.
        for defect in case["target"].get("record", {}).get("defects", []):
            if "i2c" in defect.get("lose", []):
                pytest.skip(f"Wire's pins {wire_sda} / {wire_scl}: defect {defect.get('id')} ({defect.get('why', '')})")
        # Wire anew as a controller: a test before may have left it a target (wire.slave)
        ask(dut, "controller", rb"CONTROLLER")
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
                    # dut.write and wait_reply, not ask(): a plain expect stops reading the stream, and at
                    # 25 MB/s the probe's buffers last a few hundred ms ([oep], 2026-11-18)
                    dut.write(f"clock {hz}")
                    wait_reply(dut, wait, rb"CLOCK (\d+)")
                    for address in (ADDRESS, 0x13):
                        # Above PROMISED_HZ the transfer is recorded, not required: no expectation on the wire
                        # there (the SCL that comes out is judged by judge_clocks all the same).
                        want = ([i2c(scl, sda, transactions=[{"addr": address, "rw": "write", "bytes": [], "ack": False,
                                                              "start": "S", "end": "P"}]),
                                 starts(idle), ends(idle)] if hz <= PROMISED_HZ else [])
                        name = f"{hz // 1000} kHz, address 0x{address:02x}, no target"
                        with ws_run.section(2, name, expect=want):
                            ws_run.command(f"nack 0x{address:02x}")
                            dut.write(f"nack 0x{address:02x}")
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
                            dut.write(command)
                            m = wait_reply(dut, wait, rb"SEQ (\d+) (\d+) (\S+) (\d+)")
                            ws_run.reply(m.group(0).decode())
                            first, got, hexed, last = int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4))
                            if (first, last) != (0, 0) or got != n or hexed.lower() != reply.hex():
                                wrong.append(f"{name}: endTransmission {first} / {last}, read {got} bytes {hexed}, "
                                             f"the target sent {reply.hex()}")
                            while target.read_rx()[0]:     # what the target took in: the wire is WireSkein's
                                pass
                            wait(0.01)

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


SLAVE_ADDRESS = 0x42
SOFT_SLAVE_HZ = 100_000    # softwire.slave: the master's clock; the target's write must land within its low half
                           # (tests/README.ja.md 取り込みの速さ: T = 5 us, 100 MS/s is 500 / T)


def test_wire_slave(dut, oep_host, ws_run, record_property):
    """wire.slave, softwire.slave: Wire.begin(0x42) on its default pins - or, for softwire, a SoftWire's begin(0x42)
    on the two lines open_bus picked (SDA and SCL on different pin bits: the target takes an interrupt on each) - with
    onReceive / onRequest, the probe's i2c-controller the master at 100 kHz (its pull-ups). One capture of SDA and SCL, a WireSkein section per
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
    against the probe's input, TODO). SoftWire's target follows the clock by watching the lines from the START's
    interrupt to the STOP and holds SCL low (stretches) where a callback runs, so the probe has to honour clock
    stretching (the 25 ms timeout); what has to be in time is the START's interrupt (by the end of the first bit's
    low phase) and, in each low phase, one pin write before SCL rises - SOFT_SLAVE_HZ is the clock it is asked at
    (2026-11-15: passes on V003 at 24 MHz, X035 at 48 MHz, V203)."""
    from oep_client.fixture import I2cController, I2cSegment
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    ctrl = I2cController(host)
    core.plan_apply(host, ctrl.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    record_property("probe_pullups", ctrl.internal_pullups)
    # Configured first: until then the probe's pads are idle (no pull-ups) and the lines float, and a SoftWire
    # target begun on floating lines takes their noise for STARTs (V006 starved, X035 missed the first write,
    # 2026-11-17).
    clock = ctrl.configure(SOFT_SLAVE_HZ if soft else 100_000, 25_000)
    m = ask(dut, f"slave {SLAVE_ADDRESS}", rb"SLAVE (\d)")
    if m.group(1) != b"1":
        if soft and int(sda[2:]) == int(scl[2:]):
            pytest.skip(f"n/a: SoftWire's target wants SDA and SCL on different EXTI lines, {sda} / {scl} share one")
        pytest.fail(f"begin({SLAVE_ADDRESS}) refused on {sda} / {scl}")
    wrong = []

    def got(wait):
        dut.write("sgot")
        m = wait_reply(dut, wait, rb"SGOT (\d+) (\d+) (\S+) (-?\d+) (\S+)")
        ws_run.reply(m.group(0).decode())
        return int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4)), m.group(5).decode()

    def steps(wait):
        idle = {sda: 1, scl: 1}
        A = SLAVE_ADDRESS
        with ws_run.section(1, f"{'softwire' if soft else 'wire'} slave {sda} {scl} at 0x{A:02x}, master {clock} Hz"):
            def run(name, segments, transactions, reply=None):
                with ws_run.section(2, name, expect=[i2c(scl, sda, transactions=transactions),
                                                     starts(idle), ends(idle)]):
                    if reply is not None:
                        dut.write(f"sreply {reply.hex() or '-'}")
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
        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    finally:
        end_target(dut)
        core.plan_release(host, [ctrl.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


# ---- the faults and the extensions (2026-11-17) ---------------------------------------------------------------------
# Each test is one capture of SDA and SCL, a WireSkein section per condition (tests/README.ja.md 取り込みの速さ: 100 MS/s;
# WireSkein 0.0.28 reads I2C as a spec input). The DUT is the controller at PROTOCOL_HZ against the probe's i2c-target
# (or its gpio, for a line held low), or the target of the probe's i2c-controller at 100 kHz, as above.
# SoftWire's 10-bit / general-call target runs its own bit loop from flash (SoftWireTargetExt.cpp; a plain 7-bit
# target keeps the RAM loop): the probe's controller is slowed for it (X035 with flash wait states, 2026-11-17).
SOFT_EXT_HZ = 50_000
TEN_BIT = 0x2A5           # a 10-bit address: header 11110 10 -> the 7-bit 0x7A on the wire, then the low byte 0xA5
TEN_HEADER = 0x78 | (TEN_BIT >> 8)
ABSENT = 0x13             # nobody answers it


def say(ws_run, dut, wait, command, pattern, timeout=3.0):
    """Send the DUT a console command, logged in the run, and wait for its reply."""
    ws_run.command(command)
    dut.write(command)
    m = wait_reply(dut, wait, pattern, timeout)
    ws_run.reply(m.group(0).decode())
    return m


def w(ws_run, dut, wait, address, data=b"", stop=1, ten=0, timeout=3.0):
    """endTransmission()'s result and the timeout flag."""
    m = say(ws_run, dut, wait, f"w 0x{address:x} {ten} {data.hex() or '-'} {stop}", rb"W (\d+) (\d)", timeout)
    return int(m.group(1)), int(m.group(2))


def r(ws_run, dut, wait, address, n, stop=1, ten=0):
    """requestFrom()'s count and the bytes read."""
    m = say(ws_run, dut, wait, f"r 0x{address:x} {ten} {n} {stop}", rb"R (\d+) (\S+)")
    return int(m.group(1)), bytes.fromhex(m.group(2).decode()) if m.group(2) != b"-" else b""


def pec8(data):
    """SMBus's CRC-8 (x^8 + x^2 + x + 1), initial 0."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def drain(target):
    frames = []
    while True:
        pending, data = target.read_rx()
        if data:
            frames.append(data)
        if not pending:
            return frames


NACK_DATA = bytes((0x11, 0x22, 0x33, 0x44))


def test_wire_data_nack(dut, oep_host, ws_run, record_property):
    """wire.data-nack: endTransmission() 3 when the target NACKs a data byte. The probe's i2c-target NACKs a write
    from its n-th data byte (configure(nack_from=n), oep-client 0.0.57 and later; a probe that does not declare it
    skips). The DUT writes 4 bytes for n = 1..4 and with no nack_from: 3 for every n, 0 without; on the line (WireSkein)
    the address ACKed, the bytes up to the n-th - that one NACKed - and the STOP, or all 4 ACKed."""
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    target = I2cTarget(host)
    if not target.nack_from:
        pytest.skip("the probe's i2c-target cannot NACK a data byte (no nack_from declared)")
    core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    wrong, seen = [], {}
    try:
        ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")

        def steps(wait):
            idle = {sda: 1, scl: 1}
            with ws_run.section(1, f"{'softwire' if soft else 'wire'} data nack {sda} {scl} at 0x{ADDRESS:02x}"):
                for n in (None, 1, 2, 3, 4):
                    target.configure(ADDRESS, nack_from=n)
                    sent = NACK_DATA[:n] if n else NACK_DATA
                    t = {"addr": ADDRESS, "rw": "write", "bytes": list(sent), "ack": True, "start": "S", "end": "P"}
                    if n:
                        t["nack_at"] = n - 1
                    name = f"nack_from {n}" if n else "no nack_from"
                    with ws_run.section(2, name, expect=[i2c(scl, sda, transactions=[t]), starts(idle), ends(idle)]):
                        seen[name] = w(ws_run, dut, wait, ADDRESS, NACK_DATA)[0]
                        wait(0.01)
                    while target.read_rx()[0]:          # what the target took in: the wire is WireSkein's
                        pass
                    want = 3 if n else 0
                    if seen[name] != want:
                        wrong.append(f"{name}: endTransmission {seen[name]}, want {want}")

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


def test_wire_timeout(dut, oep_host, ws_run, record_property):
    """wire.timeout: a target holding SCL low. The probe's gpio holds SCL (open drain, its pull-up on SDA): the write
    ends in 5 with getWireTimeoutFlag() set, within the bus's timeout; SCL let go, a write to an absent address is 2 -
    the bus works again with no reset of the DUT. Then, where the probe's i2c-target offers stretch, a stretch of
    three times the DUT's timeout (setWireTimeout) after a data byte: 5 and the flag; the stretch off, a write
    goes through within two tries (the automatic bus clear after a failed transfer). WireSkein: after each fault a
    clean transaction, both lines high at its start and end."""
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")
    gpio = Gpio(host)
    target = I2cTarget(host)
    wrong, seen = [], {}
    idle = {sda: 1, scl: 1}

    def steps(wait):
        with ws_run.section(1, f"{'softwire' if soft else 'wire'} timeout {sda} {scl}"):
            core.plan_apply(host, [(gpio.fn, 1, lines[ch]["channel"]) for ch in (sda, scl)])
            try:
                gpio.configure(lines[sda]["channel"], Gpio.INPUT_PULLUP)
                gpio.configure(lines[scl]["channel"], Gpio.INPUT_PULLUP)
                wait(0.02)
                with ws_run.section(2, "SCL held low by the probe"):
                    gpio.pull_low(lines[scl]["channel"])
                    wait(0.01)
                    t0 = time.monotonic()
                    seen["held"] = w(ws_run, dut, wait, ADDRESS, b"\x10")
                    seen["held_s"] = round(time.monotonic() - t0, 3)
                    gpio.configure(lines[scl]["channel"], Gpio.INPUT_PULLUP)
                    wait(0.02)
                with ws_run.section(2, "SCL let go: an absent address",
                                    expect=[i2c(scl, sda, transactions=[{"addr": ABSENT, "rw": "write", "bytes": [],
                                                                         "ack": False, "start": "S", "end": "P"}]),
                                            starts(idle), ends(idle)]):
                    seen["after"] = w(ws_run, dut, wait, ABSENT, b"\x10")
                    wait(0.01)
            finally:
                core.plan_release(host, [gpio.fn])
            if seen["held"] != (5, 1):
                wrong.append(f"SCL held: endTransmission {seen['held'][0]}, timeout flag {seen['held'][1]} (want 5, 1)")
            if seen["after"][0] != 2:
                wrong.append(f"SCL let go: endTransmission {seen['after'][0]} to an absent address (want 2)")

            if not target.offers(I2cTarget.STRETCH) or not target.max_stretch_us:
                seen["stretch"] = "the probe's i2c-target offers no stretch"
                return
            # The DUT's timeout: four byte times at PROTOCOL_HZ (a byte waits at most that long between flags), the
            # stretch three times that - both within what the probe takes.
            limit_us = 4 * 9 * 1_000_000 // PROTOCOL_HZ
            if 3 * limit_us > target.max_stretch_us:
                seen["stretch"] = f"max_stretch_us {target.max_stretch_us} < {3 * limit_us}"
                return
            core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
            try:
                target.configure(ADDRESS)
                say(ws_run, dut, wait, f"timeout {limit_us}", rb"TIMEOUT \d+")
                with ws_run.section(2, f"the target stretches {3 * limit_us} us, the DUT waits {limit_us} us"):
                    target.stretch(3 * limit_us)
                    seen["stretched"] = w(ws_run, dut, wait, ADDRESS, b"\x10\x11")
                    target.stretch(0)
                    wait(0.02)
                    seen["retries"] = [w(ws_run, dut, wait, ADDRESS, b"\x20")]
                    if seen["retries"][0][0] != 0:
                        seen["retries"].append(w(ws_run, dut, wait, ADDRESS, b"\x20"))
                    drain(target)
                with ws_run.section(2, "after the stretch",
                                    expect=[i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "write", "bytes": [0x30],
                                                                         "ack": True, "start": "S", "end": "P"}]),
                                            starts(idle), ends(idle)]):
                    seen["clean"] = w(ws_run, dut, wait, ADDRESS, b"\x30")
                    wait(0.01)
                    frames = drain(target)
                say(ws_run, dut, wait, f"timeout {CH32RV_TIMEOUT_US}", rb"TIMEOUT \d+")
            finally:
                core.plan_release(host, [target.fn])
            if seen["stretched"] != (5, 1):
                wrong.append(f"stretched past the timeout: endTransmission {seen['stretched']} (want 5, 1)")
            if seen["retries"][-1][0] != 0:
                wrong.append(f"after the stretch: {seen['retries']} - the bus was not cleared within two writes")
            if seen["clean"][0] != 0 or frames != [b"\x30"]:
                wrong.append(f"after the stretch: endTransmission {seen['clean'][0]}, the target got {frames}")

    ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    record_property("results", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


CH32RV_TIMEOUT_US = 25_000   # CH32RV_WIRE_TIMEOUT_US, the default the DUT goes back to


def test_wire_clock_stretch(dut, oep_host, ws_run, record_property):
    """wire.clock-stretch: the probe's i2c-target stretches SCL after each byte it takes (and after a read's address)
    by STRETCH_US - well past a bit time at PROTOCOL_HZ, well inside the DUT's timeout: a 3-byte write is 0 and the
    target gets the 3 bytes, a 2-byte read gets what was preloaded; WireSkein sees the transactions whole (S ... P, the
    bytes, the acknowledges) with the stretches in them (its scl_stretch notice). Skips where the probe offers no
    stretch."""
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    target = I2cTarget(host)
    if not target.offers(I2cTarget.STRETCH) or not target.max_stretch_us:
        pytest.skip("the probe's i2c-target offers no stretch (oep-if-fixture §3, optional)")
    stretch_us = min(STRETCH_US, target.max_stretch_us)
    core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    wrong, seen = [], {}
    try:
        target.configure(ADDRESS)
        target.stretch(stretch_us)
        ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")

        def steps(wait):
            idle = {sda: 1, scl: 1}
            with ws_run.section(1, f"{'softwire' if soft else 'wire'} {sda} {scl}, the target stretches {stretch_us} us"):
                data = secrets.token_bytes(3)
                with ws_run.section(2, "write 3", expect=[
                        i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "write", "bytes": list(data), "ack": True,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seen["write"] = w(ws_run, dut, wait, ADDRESS, data)
                    wait(0.01)
                    frames = drain(target)
                if seen["write"][0] != 0 or frames != [data]:
                    wrong.append(f"write 3: endTransmission {seen['write']}, the target got {frames}, sent {data.hex()}")
                reply = secrets.token_bytes(2)
                target.preload_tx(reply)
                with ws_run.section(2, "read 2", expect=[
                        i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "read", "bytes": list(reply), "ack": True,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seen["read"] = r(ws_run, dut, wait, ADDRESS, 2)
                    wait(0.01)
                if seen["read"] != (2, reply):
                    wrong.append(f"read 2: {seen['read']}, the target sent {reply.hex()}")

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps({k: [v[0], v[1].hex() if isinstance(v[1], bytes) else v[1]]
                                           for k, v in seen.items()}))
    assert not wrong, " | ".join(wrong)


STRETCH_US = 500          # wire.clock-stretch: 10 bit times at PROTOCOL_HZ, a fiftieth of the DUT's 25 ms timeout


def test_wire_bus_recovery(dut, oep_host, ws_run, record_property):
    """wire.bus-recovery: a target cut off in the middle of the byte it sends keeps SDA low. The DUT bit-bangs on the
    bus's own pins a read of the probe's i2c-target (0x00 preloaded) and stops after 3 bits of the data with SCL
    high - the target holding SDA low (STRAND: the address ACKed, SDA read low) - and reopens the bus. The next
    endTransmission() may fail (the bus is not free), and the one after it must be 0: the driver clears the bus
    itself (up to 9 clocks while SDA is low, then a STOP) after a failed transfer, with no clearBus() from the sketch.
    WireSkein: the last write clean (S, the address and its ACK, the byte, P), both lines high at its start and end."""
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    target = I2cTarget(host)
    core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    wrong, seen = [], {}
    try:
        target.configure(ADDRESS)
        ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")

        def steps(wait):
            idle = {sda: 1, scl: 1}
            with ws_run.section(1, f"{'softwire' if soft else 'wire'} bus recovery {sda} {scl}"):
                with ws_run.section(2, "the target left holding SDA, then the writes that clear it"):
                    target.preload_tx(b"\x00\x00\x00\x00")
                    m = say(ws_run, dut, wait, f"strand 0x{ADDRESS:x} 3", rb"STRAND (\d) (\d)")
                    seen["strand"] = (int(m.group(1)), int(m.group(2)))
                    seen["tries"] = [w(ws_run, dut, wait, ADDRESS, b"\x10")]
                    if seen["tries"][0][0] != 0:
                        seen["tries"].append(w(ws_run, dut, wait, ADDRESS, b"\x10"))
                    wait(0.01)
                    drain(target)
                with ws_run.section(2, "after", expect=[
                        i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "write", "bytes": [0x20], "ack": True,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seen["after"] = w(ws_run, dut, wait, ADDRESS, b"\x20")
                    wait(0.01)
                    seen["frames"] = [f.hex() for f in drain(target)]

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps(seen))
    if seen["strand"] != (1, 0):
        pytest.fail(f"the stranded read did not leave SDA low (ACK {seen['strand'][0]}, SDA {seen['strand'][1]}): "
                    "the fault was not produced")
    if seen["tries"][-1][0] != 0:
        wrong.append(f"the writes after it: {seen['tries']} - the bus was not cleared within two")
    if seen["after"][0] != 0 or seen["frames"] != ["20"]:
        wrong.append(f"after: endTransmission {seen['after'][0]}, the target got {seen['frames']}")
    assert not wrong, " | ".join(wrong)


def end_target(dut):
    """Close the target before the probe lets its lines go: a SoftWire target left listening on floating lines takes
    their noise for transfers and can keep the CPU in its interrupt (V006 stopped answering the next test's `soft`,
    2026-11-17). Best effort: a DUT that does not answer is the test's failure, reported already."""
    try:
        ask(dut, "end", rb"END", timeout=3)
    except pexpect.TIMEOUT:
        pass


def begin_target(dut, ws_run, wait, address, ten=0, soft=False, sda="", scl=""):
    m = say(ws_run, dut, wait, f"slave {address} {ten}", rb"SLAVE (\d)")
    if m.group(1) != b"1":
        if soft and int(sda[2:]) == int(scl[2:]):
            pytest.skip(f"n/a: SoftWire's target wants SDA and SCL on different EXTI lines, {sda} / {scl} share one")
        pytest.fail(f"begin({address}) refused on {sda} / {scl}")


def sgot(dut, ws_run, wait):
    m = say(ws_run, dut, wait, "sgot", rb"SGOT (\d+) (\d+) (\S+) (-?\d+) (\S+)")
    return int(m.group(1)), int(m.group(2)), m.group(3).decode(), int(m.group(4)), m.group(5).decode()


def test_wire_10bit(dut, oep_host, ws_run, record_property):
    """wire.10bit: WIRE_10BIT. As a controller: a write and a read to 0x2A5 put the header 11110 10 + W (0x7A on the
    wire, WireSkein's 7-bit reading of it) on the bus, nobody acknowledges it (the probe's i2c-target takes no reserved
    address, oep-if-fixture §3): 2 and 0. As a target at 0x2A5 with the probe's i2c-controller at 100 kHz (its 7-bit
    address field takes 0x7A, §5): a write of 3 bytes (header W, 0xA5, data) - onReceive(3) with them; a read of 2
    (header W, 0xA5, repeated START, header R, data) - what onRequest() wrote, and no onReceive for the addressing
    write; 0x2A6 (same header, another low byte) - the low byte NACKed, no callback; the 7-bit 0x42 - not
    acknowledged."""
    from oep_client.fixture import I2cController, I2cSegment
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    low = TEN_BIT & 0xFF
    wrong, seen = [], {}
    ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")
    ctrl = I2cController(host)

    def steps(wait):
        idle = {sda: 1, scl: 1}
        with ws_run.section(1, f"{'softwire' if soft else 'wire'} 10-bit 0x{TEN_BIT:03x} {sda} {scl}"):
            gpio = Gpio(host)
            core.plan_apply(host, [(gpio.fn, 1, lines[ch]["channel"]) for ch in (sda, scl)])
            try:
                for ch in (sda, scl):
                    gpio.configure(lines[ch]["channel"], Gpio.INPUT_PULLUP)
                wait(0.02)
                header_nack = [i2c(scl, sda, transactions=[{"addr": TEN_HEADER, "rw": "write", "bytes": [], "ack": False,
                                                            "start": "S", "end": "P"}]), starts(idle), ends(idle)]
                with ws_run.section(2, "controller: write, nobody there", expect=header_nack):
                    seen["write"] = w(ws_run, dut, wait, TEN_BIT, b"\x10", ten=1)
                    wait(0.01)
                with ws_run.section(2, "controller: read, nobody there", expect=header_nack):
                    seen["read"] = r(ws_run, dut, wait, TEN_BIT, 1, ten=1)
                    wait(0.01)
            finally:
                core.plan_release(host, [gpio.fn])
            if seen["write"][0] != 2 or seen["read"][0] != 0:
                wrong.append(f"controller: endTransmission {seen['write'][0]} (want 2), requestFrom {seen['read'][0]}")

            core.plan_apply(host, ctrl.assignments(lines[sda]["channel"], lines[scl]["channel"]))
            try:
                # the probe's pull-ups on before the target begins (test_wire_slave)
                seen["clock"] = ctrl.configure(SOFT_EXT_HZ if soft else 100_000, 25_000)
                begin_target(dut, ws_run, wait, TEN_BIT, 1, soft, sda, scl)
                data = secrets.token_bytes(3)
                with ws_run.section(2, "target: write 3", expect=[
                        i2c(scl, sda, transactions=[{"addr": TEN_HEADER, "rw": "write", "bytes": [low, *data],
                                                     "ack": True, "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    done = ctrl.write(TEN_HEADER, bytes([low]) + data)
                    wait(0.02)
                    got = sgot(dut, ws_run, wait)
                if not done.ok or got[0] != 1 or (got[3], got[4]) != (3, data.hex()):
                    wrong.append(f"target write 3: result {done.result}, onReceive {got[0]}, {got[3]} bytes {got[4]}")
                reply = secrets.token_bytes(2)
                say(ws_run, dut, wait, f"sreply {reply.hex()}", rb"SREPLY \d+")
                with ws_run.section(2, "target: read 2", expect=[
                        i2c(scl, sda, transactions=[
                            {"addr": TEN_HEADER, "rw": "write", "bytes": [low], "ack": True, "start": "S", "end": "Sr"},
                            {"addr": TEN_HEADER, "rw": "read", "bytes": list(reply), "ack": True, "start": "Sr",
                             "end": "P"}]), starts(idle), ends(idle)]):
                    done = ctrl.transfer([I2cSegment.write(TEN_HEADER, bytes([low])), I2cSegment.reading(TEN_HEADER, 2)])
                    wait(0.02)
                    got2 = sgot(dut, ws_run, wait)
                if not done.ok or done.data != reply or got2[:2] != (1, 1):
                    wrong.append(f"target read 2: result {done.result}, read {done.data.hex()}, callbacks {got2[:2]} "
                                 f"(want onReceive 1 - the write before - and onRequest 1)")
                with ws_run.section(2, "target: another 10-bit address", expect=[
                        i2c(scl, sda, transactions=[{"addr": TEN_HEADER, "rw": "write", "bytes": [low + 1], "ack": True,
                                                     "acks": [False], "start": "S", "end": "P"}]),
                        starts(idle), ends(idle)]):
                    other = ctrl.write(TEN_HEADER, bytes([low + 1, 0x55]))
                    wait(0.02)
                    got3 = sgot(dut, ws_run, wait)
                if other.result != I2cController.DATA_NACK or other.index != 0 or got3[:2] != got2[:2]:
                    wrong.append(f"0x{TEN_BIT + 1:03x}: result {other.result} at byte {other.index} (want the low byte "
                                 f"NACKed), callbacks {got2[:2]} -> {got3[:2]}")
                with ws_run.section(2, "target: the 7-bit 0x42", expect=[
                        i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "write", "bytes": [], "ack": False,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seven = ctrl.write(ADDRESS, b"\x01")
                    wait(0.02)
                    got4 = sgot(dut, ws_run, wait)
                if seven.result != I2cController.ADDRESS_NACK or got4[:2] != got3[:2]:
                    wrong.append(f"7-bit 0x42: result {seven.result}, callbacks {got3[:2]} -> {got4[:2]}")
            finally:
                end_target(dut)
                core.plan_release(host, [ctrl.fn])

    ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


def test_wire_general_call(dut, oep_host, ws_run, record_property):
    """wire.general-call: as a controller, a write to address 0 goes on the wire (S, 0x00 W, P - nobody answers it on
    the bench: 2); as a target at 0x42 of the probe's i2c-controller, with setGeneralCall(true) a write of 2 bytes to
    address 0 is acknowledged and reaches onReceive(2), the target's own address still answered; with it false, address
    0 is not acknowledged and no callback runs."""
    from oep_client.fixture import I2cController
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    wrong, seen = [], {}
    ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")
    ctrl = I2cController(host)

    def steps(wait):
        idle = {sda: 1, scl: 1}
        with ws_run.section(1, f"{'softwire' if soft else 'wire'} general call {sda} {scl}"):
            gpio = Gpio(host)
            core.plan_apply(host, [(gpio.fn, 1, lines[ch]["channel"]) for ch in (sda, scl)])
            try:
                for ch in (sda, scl):
                    gpio.configure(lines[ch]["channel"], Gpio.INPUT_PULLUP)
                wait(0.02)
                with ws_run.section(2, "controller: a general call", expect=[
                        i2c(scl, sda, transactions=[{"addr": 0, "rw": "write", "bytes": [], "ack": False, "start": "S",
                                                     "end": "P"}]), starts(idle), ends(idle)]):
                    seen["controller"] = w(ws_run, dut, wait, 0, b"\x06")
                    wait(0.01)
            finally:
                core.plan_release(host, [gpio.fn])
            if seen["controller"][0] != 2:
                wrong.append(f"controller: endTransmission {seen['controller'][0]} to address 0 (want 2, nobody there)")

            core.plan_apply(host, ctrl.assignments(lines[sda]["channel"], lines[scl]["channel"]))
            try:
                ctrl.configure(SOFT_EXT_HZ if soft else 100_000, 25_000)   # pull-ups first (test_wire_slave)
                begin_target(dut, ws_run, wait, ADDRESS, 0, soft, sda, scl)
                say(ws_run, dut, wait, "gc 1", rb"GC 1")
                data = secrets.token_bytes(2)
                with ws_run.section(2, "general call on", expect=[
                        i2c(scl, sda, transactions=[{"addr": 0, "rw": "write", "bytes": list(data), "ack": True,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    done = ctrl.write(0, data)
                    wait(0.02)
                    got = sgot(dut, ws_run, wait)
                if not done.ok or got[0] != 1 or (got[3], got[4]) != (2, data.hex()):
                    wrong.append(f"general call on: result {done.result}, onReceive {got[0]}, {got[3]} bytes {got[4]}")
                own = ctrl.write(ADDRESS, b"\x07")
                wait(0.02)
                got2 = sgot(dut, ws_run, wait)
                if not own.ok or got2[0] != got[0] + 1:
                    wrong.append(f"its own address with the general call on: result {own.result}, onReceive {got2[0]}")
                say(ws_run, dut, wait, "gc 0", rb"GC 0")
                with ws_run.section(2, "general call off", expect=[
                        i2c(scl, sda, transactions=[{"addr": 0, "rw": "write", "bytes": [], "ack": False, "start": "S",
                                                     "end": "P"}]), starts(idle), ends(idle)]):
                    off = ctrl.write(0, data)
                    wait(0.02)
                    got3 = sgot(dut, ws_run, wait)
                if off.result != I2cController.ADDRESS_NACK or got3[:2] != got2[:2]:
                    wrong.append(f"general call off: result {off.result}, callbacks {got2[:2]} -> {got3[:2]}")
            finally:
                end_target(dut)
                core.plan_release(host, [ctrl.fn])

    ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


def test_wire_pec(dut, oep_host, ws_run, record_property):
    """wire.pec: setPEC(true) against the probe's i2c-target at 0x42. A write of 2 bytes goes out with a third, the
    CRC-8 of [0x84, the 2 bytes] (computed here); SMBus's read word - write 0x10 with endTransmission(false), read 2 -
    takes the 2 data bytes when the target's third byte is the CRC of [0x84, 0x10, 0x85, the data] (requestFrom 2) and
    none when it is not (requestFrom 0). WireSkein: the bytes as sent and read, PEC included."""
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    target = I2cTarget(host)
    core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
    wrong, seen = [], {}
    wa, ra = ADDRESS << 1, (ADDRESS << 1) | 1
    try:
        target.configure(ADDRESS)
        ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")

        def steps(wait):
            idle = {sda: 1, scl: 1}
            with ws_run.section(1, f"{'softwire' if soft else 'wire'} PEC {sda} {scl}"):
                say(ws_run, dut, wait, "pec 1", rb"PEC 1")
                data = secrets.token_bytes(2)
                crc = pec8([wa, *data])
                with ws_run.section(2, "write 2 with PEC", expect=[
                        i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "write", "bytes": [*data, crc], "ack": True,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seen["write"] = w(ws_run, dut, wait, ADDRESS, data)
                    wait(0.01)
                    frames = drain(target)
                if seen["write"][0] != 0 or frames != [data + bytes([crc])]:
                    wrong.append(f"write: endTransmission {seen['write'][0]}, the target got "
                                 f"{[f.hex() for f in frames]}, want {(data + bytes([crc])).hex()}")
                for good in (True, False):
                    reply = secrets.token_bytes(2)
                    crc = pec8([wa, 0x10, ra, *reply])
                    sent = reply + bytes([crc if good else crc ^ 0x5A])
                    target.preload_tx(sent)
                    name = "read word, the right PEC" if good else "read word, a wrong PEC"
                    with ws_run.section(2, name, expect=[
                            i2c(scl, sda, transactions=[
                                {"addr": ADDRESS, "rw": "write", "bytes": [0x10], "ack": True, "start": "S", "end": "Sr"},
                                {"addr": ADDRESS, "rw": "read", "bytes": list(sent), "ack": True, "start": "Sr",
                                 "end": "P"}]), starts(idle), ends(idle)]):
                        first = w(ws_run, dut, wait, ADDRESS, b"\x10", stop=0)
                        got = r(ws_run, dut, wait, ADDRESS, 2)
                        wait(0.01)
                        drain(target)
                    seen[name] = [first[0], got[0], got[1].hex()]
                    want = (2, reply) if good else (0, b"")
                    if first[0] != 0 or got != want:
                        wrong.append(f"{name}: endTransmission {first[0]}, requestFrom {got[0]} {got[1].hex()}, "
                                     f"want {want[0]} {want[1].hex()}")
                say(ws_run, dut, wait, "pec 0", rb"PEC 0")

        ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


CONTEND_MS = 1500         # wire.controller-and-target: the DUT's back-to-back writes while the probe writes to it


def test_wire_controller_and_target(dut, oep_host, ws_run, record_property):
    """wire.controller-and-target: Wire begun as a target at 0x42 is a controller too (one instance). In turn: the
    probe's i2c-controller writes to 0x42 (onReceive with the bytes); the probe's i2c-target at 0x43 takes a write of
    the DUT (0, the bytes; the DUT still a target); the probe writes to 0x42 again (answered). Then both at once:
    for CONTEND_MS the DUT writes back to back to an absent address while the probe writes to 0x42 as fast as the
    host sends. The probe does not wait for the bus to be free (2026-11-17: it starts in the middle of the DUT's
    transfers), so the collisions are recorded, not judged; judged: the DUT never hangs, its results are mostly 2 (the
    rest counted), its onReceive ran at least once per probe write that was done, and
    afterwards the probe's write is answered and the DUT's write to an absent address is a clean 2 (WireSkein).
    SoftWire: n/a (one role at a time)."""
    from oep_client.fixture import I2cController
    from wireskein.runlog import ends, i2c, starts
    case = settings(__file__)
    if case.get("case") == "softwire":
        pytest.skip("n/a: a SoftWire is a controller or a target, one at a time (SoftWire.h)")
    lines, sda, scl, soft = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    ctrl, target = I2cController(host), I2cTarget(host)
    wrong, seen = [], {}
    ask(dut, f"clock {PROTOCOL_HZ}", rb"CLOCK (\d+)")

    def probe_writes(wait, idle):
        core.plan_apply(host, ctrl.assignments(lines[sda]["channel"], lines[scl]["channel"]))
        ctrl.configure(100_000, 25_000)
        data = secrets.token_bytes(2)
        before = sgot(dut, ws_run, wait)[0]
        with ws_run.section(2, "the probe writes to the DUT", expect=[
                i2c(scl, sda, transactions=[{"addr": ADDRESS, "rw": "write", "bytes": list(data), "ack": True,
                                             "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
            done = ctrl.write(ADDRESS, data)
            wait(0.02)
            got = sgot(dut, ws_run, wait)
        if not done.ok or got[0] != before + 1 or (got[3], got[4]) != (2, data.hex()):
            wrong.append(f"the probe's write: result {done.result}, onReceive {before} -> {got[0]}, {got[3]} bytes {got[4]}")

    def steps(wait):
        idle = {sda: 1, scl: 1}
        with ws_run.section(1, f"wire controller and target {sda} {scl}"):
            core.plan_apply(host, ctrl.assignments(lines[sda]["channel"], lines[scl]["channel"]))
            ctrl.configure(100_000, 25_000)   # the probe's pull-ups on before the target begins (test_wire_slave)
            begin_target(dut, ws_run, wait, ADDRESS)
            core.plan_release(host, [ctrl.fn])
            probe_writes(wait, idle)
            core.plan_release(host, [ctrl.fn])

            core.plan_apply(host, target.assignments(lines[sda]["channel"], lines[scl]["channel"]))
            try:
                target.configure(ADDRESS + 1)
                data = secrets.token_bytes(2)
                with ws_run.section(2, "the DUT, a target, writes to the probe", expect=[
                        i2c(scl, sda, transactions=[{"addr": ADDRESS + 1, "rw": "write", "bytes": list(data),
                                                     "ack": True, "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seen["dut write"] = w(ws_run, dut, wait, ADDRESS + 1, data)
                    wait(0.01)
                    frames = drain(target)
                if seen["dut write"][0] != 0 or frames != [data]:
                    wrong.append(f"the DUT's write: endTransmission {seen['dut write'][0]}, the probe got "
                                 f"{[f.hex() for f in frames]}")
            finally:
                core.plan_release(host, [target.fn])

            try:
                probe_writes(wait, idle)
                before = sgot(dut, ws_run, wait)[0]
                results = {}
                with ws_run.section(2, f"both controllers for {CONTEND_MS} ms", expect=[ends(idle)]):
                    ws_run.command(f"contend {CONTEND_MS} 0x{ABSENT:x}")
                    dut.write(f"contend {CONTEND_MS} 0x{ABSENT:x}")
                    end = time.monotonic() + CONTEND_MS / 1000 * 0.8
                    while time.monotonic() < end:
                        done = ctrl.write(ADDRESS, b"\xa5\x5a")
                        results[done.result] = results.get(done.result, 0) + 1
                    m = wait_reply(dut, wait, rb"CONTEND (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)", timeout=CONTEND_MS / 1000 + 5)
                    ws_run.reply(m.group(0).decode())
                    wait(0.02)
                after = sgot(dut, ws_run, wait)[0]
            finally:
                core.plan_release(host, [ctrl.fn])
            counts = dict(zip(("0", "2", "3", "4", "5", "other"), (int(x) for x in m.groups())))
            seen.update(dut=counts, probe={str(k): v for k, v in results.items()}, received=after - before)
            # The probe's i2c-controller starts a transfer when the host asks, without waiting for the bus to be
            # free (2026-11-17: most of its writes collided, some cut the DUT's mid-byte), so it is not the
            # multi-master peer UM10204 describes: what comes out of a collision is recorded, not judged. Judged:
            # the DUT never hangs, its results are mostly 2, its target
            # took at least every write the probe finished, and both roles work cleanly once the probe stops.
            # (a 3 or even a 0 can come out too: the probe's address bits overlapping the DUT's ACK slot read as an
            # ACK - V205 / X035 once each, 2026-11-17)
            if counts["other"] or not counts["2"]:
                wrong.append(f"contention: the DUT's results {counts} (mostly 2, nothing but 0-5)")
            if after - before < results.get(I2cController.DONE, 0):
                wrong.append(f"contention: onReceive ran {after - before} times for "
                             f"{results.get(I2cController.DONE, 0)} writes the probe finished")
            try:
                probe_writes(wait, idle)
            finally:
                core.plan_release(host, [ctrl.fn])
            gpio = Gpio(host)
            core.plan_apply(host, [(gpio.fn, 1, lines[ch]["channel"]) for ch in (sda, scl)])
            try:
                for ch in (sda, scl):
                    gpio.configure(lines[ch]["channel"], Gpio.INPUT_PULLUP)
                wait(0.02)
                # the section inside the pull-ups: let go after it, the lines fall (V203, 2026-11-17)
                with ws_run.section(2, "the DUT after the contention, an absent address", expect=[
                        i2c(scl, sda, transactions=[{"addr": ABSENT, "rw": "write", "bytes": [], "ack": False,
                                                     "start": "S", "end": "P"}]), starts(idle), ends(idle)]):
                    seen["after"] = w(ws_run, dut, wait, ABSENT, b"\x01")
                    wait(0.01)
            finally:
                end_target(dut)
                core.plan_release(host, [gpio.fn])
            if seen["after"][0] != 2:
                wrong.append(f"after the contention: endTransmission {seen['after'][0]} to an absent address (want 2)")

    ws_capture.stream(ws_run, oep_host, [lines[sda], lines[scl]], rate=PROTOCOL_RATE, steps=steps)
    record_property("results", json.dumps(seen))
    record_property("collisions", json.dumps({"probe_lost": seen.get("probe", {}).get(str(I2cController.BUS_ERROR), 0),
                                              "dut_gave_up": seen.get("dut", {}).get("4", 0)}))
    assert not wrong, " | ".join(wrong)


def test_wire_arbitration_lost(dut, oep_host):
    """wire.arbitration: the DUT losing arbitration on purpose - its START within one SCL bit of another controller's
    and its address losing a 1 bit to their 0 - retried (Wire.h, CH32RV_WIRE_ARBITRATION_RETRIES) and reported as 4
    when lost every time. Not producible on purpose: the probe's i2c-controller starts a transfer when the host's
    request arrives (milliseconds), so it cannot put its START within a bit time of the DUT's;
    wire.controller-and-target only counts the collisions that happen."""
    pytest.skip("the probe's i2c-controller cannot time its START against the DUT's: arbitration on purpose needs a "
                "transfer started by a line event (on SDA falling - the DUT's START - with no delay of the host), or "
                "an i2c-target / gpio option to pull SDA low at a given bit of the next transfer - asked of [oep] "
                "2026-11-17, awaiting its decision")
