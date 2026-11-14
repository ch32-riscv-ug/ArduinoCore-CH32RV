"""spi.modes (case spi): the hardware SPI on its default pins; softspi.modes (case softspi): a SoftSPI. Either is the
controller of the OEP probe's spi-target (tests/sketch_support/spi/spi.ino).

The lines are `<port>@<pad>` ones the probe labels (bench_lines.py gpio_lines): for the hardware SPI the three the
DUT names as its default SCK / MISO / MOSI (spipins), which must all be labelled, and CS the first other line; for
SoftSPI the first four lines. Without them the test skips, naming what is missing.

In each of the four modes and both bit orders at 250 kHz, and mode 0 MSB first at 1 MHz, the sketch sends three
bytes with transfer() and a word with transfer16(), CS low around them, while the target answers five armed bytes.
spi.output-only runs first with no target: the probe only captures CS, SCK and MOSI, so a fault there is the DUT's
own, and SCK must stay still while CS is high except the one move of a changed idle level (CPOL). The round trip
(test_spi_contract) then adds the probe's spi-target: one capture of CS, SCK, MOSI and MISO streams through all of them (ws_capture.stream), a WireSkein section per
condition, judged there (spi): the mode on the lines, 40 clocks while CS is low, no SCK glitch, MOSI = the bytes then the word high byte first under MSBFIRST and low byte first under LSBFIRST (as the
hardware SPI and AVR send it), MISO = the armed bytes; CS high at the condition's start and end. What the sketch
read back (its XFER reply) is compared here: the armed bytes, and the word assembled the same way. SCK moves
while CS is high only once when the mode's idle level (CPOL) changes: beginTransaction() sets it before CS falls,
as Arduino's SPI does (WireSkein's sck_outside_cs notice: one allowed then, ng otherwise)."""
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
from oep_client.fixture import SpiTarget

RATE = 10_000_000          # 20 samples a half period at 250 kHz, 5 at 1 MHz
# The slave's MISO changes some 25 ns after the sampling edge at each byte boundary (CH32V103, 2026-11-07): at
# 10 MS/s that falls in the edge's own sample and WireSkein reads it as changing at the edge (no mode reads it).
SLAVE_RATE = 40_000_000
CONDITIONS = [(mode, lsb, 250_000) for mode in range(4) for lsb in (0, 1)] + [(0, 0, 1_000_000)]


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def pad(number):
    return f"P{'ABCDEFGHIJK'[(number >> 5) - 2]}{number & 31}"


def far_from(candidates, lines, ref):
    """The candidate whose probe channel is furthest from ref's: CS next to SCK picked up its edges on 6-2's harness
    (a short high in the middle of CS split a frame, 2026-11-14), and the channels run along the probe's header."""
    return max(candidates, key=lambda p: abs(lines[p]["channel"] - lines[ref]["channel"]))


def spread(lines):
    """SoftSPI's four lines, as far apart on the probe as the labels allow (sck, miso, mosi, cs): side by side on
    6-2's harness, CS and MISO picked up the edges of the lines next to them."""
    pads = sorted(lines, key=lambda p: lines[p]["channel"])
    if len(pads) <= 4:
        return tuple(pads[:4])
    step = (len(pads) - 1) / 3
    picked = [pads[round(i * step)] for i in range(4)]
    return picked[0], picked[2], picked[1], picked[3]


def open_bus(dut, oep_host, case, record_property):
    """Start the case's bus on the DUT and pick its lines: (lines by pad, sck, miso, mosi, cs, soft)."""
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    soft = case.get("case") == "softspi"
    lines = {line["pad"]: line for line in gpio_lines(oep_host, case["target"])}
    if soft:
        if len(lines) < 4:
            pytest.skip(f"SoftSPI wants four <port>@<pad> lines, the probe labels {len(lines)}")
        sck, miso, mosi, cs = spread(lines)
        assert ask(dut, f"pins {lines[sck]['pin']} {lines[miso]['pin']} {lines[mosi]['pin']} {lines[cs]['pin']}",
                   rb"PINS (\d)").group(1) == b"1", "SoftSPI refused the pins"
        record_property("bus", "SoftSPI")
    else:
        m = ask(dut, "spipins", rb"SPIPINS (NONE|\d+ \d+ \d+)")
        if m.group(1) == b"NONE":
            pytest.skip("the part has no SPI")
        sck, miso, mosi = (pad(int(n)) for n in m.group(1).split())
        missing = [p for p in (sck, miso, mosi) if p not in lines]
        if missing:
            pytest.skip(f"the SPI's default pins {sck} / {miso} / {mosi}: the probe labels no "
                        + ", ".join(f"<port>@{p.lower()}" for p in missing))
        others = [p for p in lines if p not in (sck, miso, mosi)]
        if not others:
            pytest.skip("no other <port>@<pad> line for CS")
        cs = far_from(others, lines, sck)
        ask(dut, f"hw {lines[cs]['pin']}", rb"HW (\d+) (\d+) (\d+)")
        record_property("bus", "SPI")
    record_property("pins", json.dumps({"sck": sck, "miso": miso, "mosi": mosi, "cs": cs}))
    return lines, sck, miso, mosi, cs, soft


def wait_reply(dut, wait, pattern, timeout=3.0):
    """The DUT's reply, looked for in short tries with wait() between so the stream keeps being read."""
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def test_spi_output_only(dut, oep_host, ws_run, record_property):
    """The bus with no target: the probe only captures CS, SCK and MOSI (MISO floats and is not judged), so what is
    seen is the DUT's own doing - the baseline the round trip below is compared with when something looks wrong.
    Every condition of the round trip, judged the same way, and SCK must stay still while CS is high (WireSkein's
    sck_outside_cs notice: ng) unless the mode's idle level (CPOL) changes, which beginTransaction() makes as one
    move before CS falls (one notice allowed)."""
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)

    def steps(wait):
        idle = {cs: 1}
        cpol = 0                                       # the bus begins in mode 0
        with ws_run.section(1, f"{'softspi' if soft else 'spi'} output only {sck} {mosi} {cs}"):
            for mode, lsb, hz in CONDITIONS:
                name = f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST {hz // 1000} kHz"
                out = secrets.token_bytes(3)
                word = int.from_bytes(secrets.token_bytes(2), "big")
                wire_word = word.to_bytes(2, "little" if lsb else "big")
                same_idle = (mode >> 1) == cpol
                cpol = mode >> 1
                want = [spi(sck, mosi=mosi, cs=cs, mode=mode, bit_order="lsb" if lsb else "msb",
                            mosi_bytes=(out + wire_word).hex(), bits=40,
                            notices={"sck_outside_cs": "ng" if same_idle else 1}),
                        starts(idle), ends(idle)]
                with ws_run.section(2, name, expect=want):
                    command = f"xfer {mode} {lsb} {hz} {word:04x} {out.hex()}"
                    ws_run.command(command)
                    dut.write(command + "\n")
                    ws_run.reply(wait_reply(dut, wait, rb"XFER \S+ [0-9a-f]{4}").group(0).decode())
                    wait(0.01)

    ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi)], rate=RATE, steps=steps)


def test_spi_contract(dut, oep_host, ws_run, record_property):
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    ch = {role: lines[p]["channel"] for role, p in (("sck", sck), ("miso", miso), ("mosi", mosi), ("cs", cs))}

    host = oep_host.host
    target = SpiTarget(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]))
    orders = [0, 1] if target.features & SpiTarget.FEATURE_LSB_FIRST else [0]
    record_property("orders", json.dumps(orders))
    read_wrong = []

    def steps(wait):
        idle = {cs: 1}
        cpol = 0                                       # the bus begins in mode 0
        with ws_run.section(1, f"{'softspi' if soft else 'spi'} {sck} {miso} {mosi} {cs}"):
            for mode, lsb, hz in CONDITIONS:
                if lsb not in orders:
                    continue
                same_idle = (mode >> 1) == cpol
                cpol = mode >> 1
                name = f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST {hz // 1000} kHz"
                out = secrets.token_bytes(3)
                word = int.from_bytes(secrets.token_bytes(2), "big")
                armed = secrets.token_bytes(5)
                wire_word = word.to_bytes(2, "little" if lsb else "big")
                want = [spi(sck, mosi=mosi, miso=miso, cs=cs, mode=mode, bit_order="lsb" if lsb else "msb",
                            mosi_bytes=(out + wire_word).hex(), miso_bytes=armed.hex(), bits=40,
                            notices={"sck_outside_cs": "ng" if same_idle else 1}),
                        starts(idle), ends(idle)]
                with ws_run.section(2, name, expect=want):
                    target.configure(mode=mode, bit_order=lsb)
                    target.arm(5, armed)
                    command = f"xfer {mode} {lsb} {hz} {word:04x} {out.hex()}"
                    ws_run.command(command)
                    dut.write(command + "\n")
                    m = wait_reply(dut, wait, rb"XFER (\S+) ([0-9a-f]{4})")
                    ws_run.reply(m.group(0).decode())
                    got_bytes, got_word = bytes.fromhex(m.group(1).decode()), int(m.group(2), 16)
                    want_word = int.from_bytes(armed[3:5], "little" if lsb else "big")
                    if got_bytes != armed[:3] or got_word != want_word:
                        read_wrong.append(f"{name}: read {got_bytes.hex()} {got_word:04x}, "
                                          f"the target was armed with {armed[:3].hex()} {want_word:04x}")
                    wait(0.01)

    lines_in_order = [lines[p] for p in (cs, sck, mosi, miso)]
    try:
        ws_capture.stream(ws_run, oep_host, lines_in_order, rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("read", json.dumps(read_wrong) if read_wrong else "ok")
    assert not read_wrong, " | ".join(read_wrong)


SLAVE_HZ = 100_000                 # within what CH32RVSPISlave's README measured (100-200 kHz, one interrupt a byte)
SLAVE_SETUP_NS = 20_000            # CS falling to the first clock: the CS interrupt selects the slave
SLAVE_FILL = 0xA5


def test_spi_slave(dut, oep_host, ws_run, record_property):
    """spi.slave (case spi only): CH32RVSPISlave on the SPI's default pins, CS the first other labelled line, the
    probe's spi-controller the master at 100 kHz with 20 us from CS to the first clock. One capture of the four lines
    at 40 MS/s (SLAVE_RATE),
    a WireSkein section per condition judging the bus (spi: the mode, the clocks, MOSI = what the probe sent, MISO =
    what the slave queued, then its fill byte); what the slave kept (its Result: length and bytes) and what the
    probe read are compared here. In each of the four modes and both bit orders, 8 bytes queued and 8 clocked; then
    in mode 0: the master clocking 10 for 8 queued (length 10, 8 kept, the fill byte after tx), and a chip select with
    nothing queued (the fill byte throughout, missed() one more). SCK moves while CS is high only where the master's
    idle level (CPOL) changes."""
    from oep_client.fixture import SpiController, SpiItem
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    if case.get("case") != "spi":
        pytest.skip("the slave is the hardware SPI's")
    lines, sck, miso, mosi, cs, _ = open_bus(dut, oep_host, case, record_property)
    host = oep_host.host
    ctrl = SpiController(host)
    core.plan_apply(host, ctrl.assignments(lines[sck]["channel"], lines[mosi]["channel"], lines[miso]["channel"],
                                           lines[cs]["channel"]))
    orders = [0, 1] if ctrl.features & SpiController.FEATURE_LSB_FIRST else [0]
    record_property("orders", json.dumps(orders))
    conditions = [(mode, lsb, 8, 8, True) for mode in range(4) for lsb in orders]
    conditions += [(0, 0, 8, 10, True), (0, 0, 8, 8, False)]     # clocked past what was queued; nothing queued
    # CH32V103's and CH32V20x's slave MISO changes 0..25 ns after the sampling edge at byte boundaries, within
    # their datasheets (output hold th(SO) 15 ns min): a change in the edge's own sample at SLAVE_RATE counts as
    # after the edge, and WireSkein records it (hold_under_sample). V203 (5-1) fell to it 3 runs in 3 on
    # 2026-11-14 ("reads in no mode", hold 0 samples; 25 ns on 11-13, when it passed - [oep]'s reading).
    edge_sample = "before" if case["target"].get("family") in ("CH32V103", "CH32V20x") else None
    wrong = []

    def steps(wait):
        idle = {cs: 1}
        cpol = None
        with ws_run.section(1, f"spi slave {sck} {miso} {mosi} {cs}"):
            for mode, lsb, size, clocked, queued in conditions:
                name = (f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST {size} queued {clocked} clocked"
                        if queued else f"mode {mode} nothing queued")
                order = "lsb" if lsb else "msb"
                same_idle = cpol is not None and (mode >> 1) == cpol
                cpol = mode >> 1
                sent = secrets.token_bytes(clocked)
                armed = secrets.token_bytes(size)
                back = (armed + bytes([SLAVE_FILL]) * clocked)[:clocked] if queued else bytes([SLAVE_FILL]) * clocked
                want = [spi(sck, mosi=mosi, miso=miso, cs=cs, mode=mode, bit_order=order, mosi_bytes=sent.hex(),
                            miso_bytes=back.hex(), bits=8 * clocked,
                            notices={"sck_outside_cs": "ng" if same_idle else 1}, edge_sample=edge_sample),
                        starts(idle), ends(idle)]
                with ws_run.section(2, name, expect=want):
                    ctrl.configure(mode=mode, bit_order=lsb, clock_hz=SLAVE_HZ)
                    for command, reply in ((f"slave {lines[cs]['pin']} {mode} {lsb} {SLAVE_FILL:02x}", rb"SLAVE (\d)"),
                                           *(((f"sq {size} {armed.hex()}", rb"SQ (\d)"),) if queued else ())):
                        ws_run.command(command)
                        dut.write(command + "\n")
                        m = wait_reply(dut, wait, reply)
                        ws_run.reply(m.group(0).decode())
                        assert m.group(1) == b"1", f"{name}: {command} refused"
                    wait(0.01)
                    got = ctrl.transfer([SpiItem(sent, 0, SLAVE_SETUP_NS, SLAVE_SETUP_NS)])[0].rx
                    wait(0.01)
                    ws_run.command("sres")
                    dut.write("sres\n")
                    m = wait_reply(dut, wait, rb"SRES (\S+) (\S+) (\d+) (\d+)")
                    ws_run.reply(m.group(0).decode())
                    length, kept, missed, overruns = m.groups()
                    if queued:
                        want_kept = sent[:size].hex()
                        if (length.decode(), kept.decode(), overruns) != (str(clocked), want_kept, b"0") or missed != b"0":
                            wrong.append(f"{name}: the slave kept {length.decode()} {kept.decode()} (missed "
                                         f"{missed.decode()}, overruns {overruns.decode()}), want {clocked} {want_kept}")
                    elif (length, missed) != (b"NONE", b"1"):
                        wrong.append(f"{name}: sres {m.group(0).decode()}, want no result and missed 1")
                    if got != back:
                        wrong.append(f"{name}: the probe read {got.hex()}, want {back.hex()}")
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi, miso)], rate=SLAVE_RATE, steps=steps)
    finally:
        core.plan_release(host, [ctrl.fn])
        dut.write("send\n")
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)
