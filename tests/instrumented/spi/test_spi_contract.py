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
as Arduino's SPI does (WireSkein's sck_outside_cs notice: one allowed then, ng otherwise).

The DUT as the peripheral of the probe's spi-controller: spi.slave / softspi.slave (CH32RVSPISlave / SoftSPISlave,
transfers framed by CS) and spi.peripheral / softspi.peripheral (the bus itself, SPI_HAS_PERIPHERAL_MODE) - see
their tests."""
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
import bench_ready   # staged beside the test (run_hardware.py)

RATE = 20_000_000          # tests/README.ja.md 取り込みの速さ: T = 500 ns, 1 MHz SCK's half period, 10 / T (it was 40 MS/s,
                           # 20 / T: 4 lines at 20 MB/s outran the host's reading at a load of 11 and the probe dropped
                           # segments, [oep] 2026-11-19). CS glitches under 12.5 ns (6-2) are below 5 / T of any rate the
                           # probe streams: not judged here
# The slave's MISO changes some 25 ns after the sampling edge at each byte boundary (CH32V103, 2026-11-07): at
# 10 MS/s that falls in the edge's own sample and WireSkein reads it as changing at the edge (no mode reads it).
SLAVE_RATE = 40_000_000    # T = 5 us (100 kHz SCK's half period) and the 20 us CS setup: well over 20 / T
CONDITIONS = [(mode, lsb, 250_000) for mode in range(4) for lsb in (0, 1)] + [(0, 0, 1_000_000)]


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line)
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def pad(number):
    return f"P{'ABCDEFGHIJK'[(number >> 5) - 2]}{number & 31}"


def far_from(candidates, lines, refs):
    """The candidate whose probe channel is furthest from the nearest of refs' (the bus's other lines): CS beside
    SCK, then beside MOSI, picked up their edges on 6-2's harness - a high of 12.5 ns or less in the middle of CS,
    which the probe's SPI target took for the end of the frame ([oep], 2026-11-14) - and the channels run along the
    probe's header. The probe's channel numbers do not tell what runs beside what (PD6 is channel 54, PD7 4), so first
    the DUT's pads: another port is furthest, on the same port the bits apart; then the channels."""
    return max(candidates, key=lambda p: (min(pad_gap(p, r) for r in refs),
                                          min(abs(lines[p]["channel"] - lines[r]["channel"]) for r in refs)))


def pad_gap(a, b):
    """How far apart two pads are on the DUT: another port is furthest, on the same port the bits apart."""
    return 99 if a[:2].upper() != b[:2].upper() else abs(int(a[2:]) - int(b[2:]))


def spread(lines):
    """SoftSPI's four lines (sck, miso, mosi, cs), as far apart as the labels allow where it matters: an edge on a
    line beside SCK or CS reaches it (6-2's harness: CS beside SCK split a frame in two, PD5 / PD6, 2026-11-18), so
    SCK is put furthest from CS, then the pair furthest from MOSI; MISO, which the target drives and nobody samples
    on an edge, takes what is left. The probe's channel numbers do not tell what runs beside what: the DUT's pads
    first, the channels after."""
    import itertools
    pads = sorted(lines, key=lambda p: lines[p]["channel"])
    if len(pads) < 4:
        return tuple(pads)

    def score(roles):
        sck, miso, mosi, cs = roles
        chan = lambda a, b: abs(lines[a]["channel"] - lines[b]["channel"])
        return (pad_gap(sck, cs), min(pad_gap(sck, mosi), pad_gap(cs, mosi)),
                chan(sck, cs), min(chan(sck, mosi), chan(cs, mosi)))
    return max(itertools.permutations(pads, 4), key=score)


def open_bus(dut, oep_host, case, record_property):
    """Start the case's bus on the DUT and pick its lines: (lines by pad, sck, miso, mosi, cs, soft)."""
    nonce = secrets.token_hex(4)
    m = bench_ready.ready(dut, rb" (\S+)", nonce=nonce)
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
        named = ((case["target"].get("record") or {}).get("spi") or {}).get("cs", "").upper()
        if named and named not in lines:
            pytest.fail(f"the record's spi.cs {named} is not a labelled line of the port")
        cs = named or far_from(others, lines, (sck, miso, mosi))
        ask(dut, f"hw {lines[cs]['pin']}", rb"HW (\d+) (\d+) (\d+)")
        record_property("bus", "SPI")
    record_property("pins", json.dumps({"sck": sck, "miso": miso, "mosi": mosi, "cs": cs}))
    return lines, sck, miso, mosi, cs, soft


def configure_target(target, mode, lsb, soft):
    """The probe's spi-target in this mode and bit order. A SoftSPI's SCK is a GPIO with slow edges (H417's 10 MHz
    pads) that the target counted twice now and then: there it filters SCK pulses under 25 ns (sck_filter, oep-spec
    baa2605 fixture section 4; [oep] 2026-11-18), which holds up to about 16 MHz."""
    if soft:
        target.configure(mode=mode, bit_order=lsb, sck_filter=True)
    else:
        target.configure(mode=mode, bit_order=lsb)


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
                    dut.write(command)
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
                    configure_target(target, mode, lsb, soft)
                    target.arm(5, armed)
                    command = f"xfer {mode} {lsb} {hz} {word:04x} {out.hex()}"
                    ws_run.command(command)
                    dut.write(command)
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


BLOCK_BYTES = 64                   # spi.block: over the 32 the CPU moved and DMA took in the trials (2026-11-18)
BLOCK_HZ = 1_000_000
BLOCK_HOWS = {"w": "writeBytes", "t": "transferBytes", "b": "transfer(buf, n)",
              "a": "transferAsync out only", "A": "transferAsync both ways"}


def test_spi_block(dut, oep_host, ws_run, record_property):
    """The buffer transfers: writeBytes, transferBytes and transfer(buf, n) (the hardware bus's own loop, SPI.h),
    and transferAsync out only and both ways (its DMA; SoftSPI does it at once). Each sends 64 random bytes in mode 0
    MSB first, CS low around the one call, while the probe's spi-target answers 64 armed bytes: on the wire
    (WireSkein, spi) MOSI = the bytes, MISO = the armed ones, 512 clocks; what the calls that read gave back
    (BLK) = the armed bytes. transferAsync must start (BLK 1): the bench's sketch holds no other DMA channel."""
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    ch = {role: lines[p]["channel"] for role, p in (("sck", sck), ("miso", miso), ("mosi", mosi), ("cs", cs))}
    host = oep_host.host
    target = SpiTarget(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]))
    wrong = []

    def steps(wait):
        idle = {cs: 1}
        with ws_run.section(1, f"{'softspi' if soft else 'spi'} block {sck} {miso} {mosi} {cs}"):
            for how, name in BLOCK_HOWS.items():
                out = secrets.token_bytes(BLOCK_BYTES)
                armed = secrets.token_bytes(BLOCK_BYTES)
                want = [spi(sck, mosi=mosi, miso=miso, cs=cs, mode=0, bit_order="msb",
                            mosi_bytes=out.hex(), miso_bytes=armed.hex(), bits=8 * BLOCK_BYTES,
                            notices={"sck_outside_cs": "ng"}),
                        starts(idle), ends(idle)]
                with ws_run.section(2, name, expect=want):
                    configure_target(target, 0, 0, soft)
                    target.arm(BLOCK_BYTES, armed)
                    command = f"blk {how} {BLOCK_HZ} {out.hex()}"
                    ws_run.command(command)
                    dut.write(command)
                    m = wait_reply(dut, wait, rb"BLK ([01]) (-|[0-9a-f]+)")
                    ws_run.reply(m.group(0).decode())
                    if m.group(1) != b"1":
                        wrong.append(f"{name}: did not start")
                    elif m.group(2) != b"-" and bytes.fromhex(m.group(2).decode()) != armed:
                        wrong.append(f"{name}: read {m.group(2).decode()}, the target was armed with {armed.hex()}")
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi, miso)], rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("read", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


SHIFT_BYTES = 3


def test_shift(dut, oep_host, ws_run, record_property):
    """shiftOut() and shiftIn() on SoftSPI's lines (the softspi case; CS an ordinary GPIO around them): 3 random bytes
    out (MOSI as data, SCK as clock), then 3 in from MISO, MSB first and LSB first, while the probe's spi-target
    (mode 0, which is how both move the clock: data set while SCK is low, read while it is high) answers 6 armed
    bytes. On the wire (WireSkein, spi, mode 0, the section's clocks): MOSI = the bytes out, then the last bit held
    through shiftIn(); MISO = the armed bytes; CS high and SCK low at the section's ends. What shiftIn() gave back (SHIFT) = the armed bytes 4 to 6 (bit-reversed for LSB first:
    the target sends MSB first). The clock is as fast as digitalWrite(); its rate is not part of the contract."""
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    if case.get("case") != "softspi":
        pytest.skip("shiftOut() / shiftIn() are judged on the softspi case's lines")
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    ch = {role: lines[p]["channel"] for role, p in (("sck", sck), ("miso", miso), ("mosi", mosi), ("cs", cs))}
    host = oep_host.host
    target = SpiTarget(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]))
    wrong = []

    def rev(b):
        return int(f"{b:08b}"[::-1], 2)

    def steps(wait):
        idle = {cs: 1, sck: 0}
        with ws_run.section(1, f"shiftOut / shiftIn {sck} {miso} {mosi} {cs}"):
            for lsb in (0, 1):
                out = secrets.token_bytes(SHIFT_BYTES)
                armed = secrets.token_bytes(2 * SHIFT_BYTES)
                last = (out[-1] >> 7) & 1 if lsb else out[-1] & 1
                held = bytes([0xFF if last else 0x00]) * SHIFT_BYTES
                on_wire = armed if not lsb else bytes(rev(b) for b in armed)
                # No cs for spi(): within CS it calls an SCK level under half the median a glitch, and shiftOut()'s
                # high is one digitalWrite() against two for its low (data, then clock) - 200 ns against 450 on
                # V205, right at that line (2026-11-20). The duty is not shiftOut()'s contract; the section holds
                # this transfer alone, CS's levels are judged by starts / ends.
                want = [spi(sck, mosi=mosi, miso=miso, mode=0, bit_order="lsb" if lsb else "msb",
                            mosi_bytes=(out + held).hex(), miso_bytes=on_wire.hex()),
                        starts(idle), ends(idle)]
                name = "LSBFIRST" if lsb else "MSBFIRST"
                with ws_run.section(2, name, expect=want):
                    configure_target(target, 0, 0, True)
                    target.arm(2 * SHIFT_BYTES, armed)
                    command = f"shift {lsb} {out.hex()} {SHIFT_BYTES}"
                    ws_run.command(command)
                    dut.write(command)
                    m = wait_reply(dut, wait, rb"SHIFT (NONE|[0-9a-f]*)")
                    ws_run.reply(m.group(0).decode())
                    if m.group(1) != on_wire[SHIFT_BYTES:].hex().encode():
                        wrong.append(f"{name}: shiftIn() read {m.group(1).decode()}, "
                                     f"the target sent {on_wire[SHIFT_BYTES:].hex()} ({name})")
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi, miso)], rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("read", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


def test_spi_hw_cs(dut, oep_host, ws_run, record_property):
    """SPI.setHwCs(true) (arduino-esp32's): the SPI drives its NSS pad itself, low from beginTransaction() to
    endTransaction(), the sketch's GPIO CS left high. On the default route's NSS pad (PIN_SPI_SS), when the probe
    labels it: writeBytes and transferBytes of 64 bytes as in test_spi_block, the probe's spi-target framed by that
    pad, judged by WireSkein (CS high at the start and the end, the bytes and 512 clocks) and the bytes read back."""
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    if case.get("case") == "softspi":
        pytest.skip("setHwCs() is the hardware SPI's")
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    taken, nss_pin = (int(x) for x in ask(dut, "hwcs 1", rb"HWCS (\d) (\d+)").groups())
    if not taken:
        pytest.skip("setHwCs() did not take: the route has no NSS pad")
    nss = pad(nss_pin)
    if nss not in lines:
        ask(dut, "hwcs 0", rb"HWCS \d \d+")
        pytest.skip(f"the NSS pad {nss} is not a labelled line of the port")
    record_property("nss", nss)
    ch = {role: lines[p]["channel"] for role, p in (("sck", sck), ("miso", miso), ("mosi", mosi), ("cs", nss))}
    host = oep_host.host
    target = SpiTarget(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]))
    wrong = []

    def steps(wait):
        idle = {nss: 1}
        with ws_run.section(1, f"spi hardware cs {sck} {miso} {mosi} {nss}"):
            for how, name in (("w", "writeBytes"), ("t", "transferBytes")):
                out = secrets.token_bytes(BLOCK_BYTES)
                armed = secrets.token_bytes(BLOCK_BYTES)
                want = [spi(sck, mosi=mosi, miso=miso, cs=nss, mode=0, bit_order="msb",
                            mosi_bytes=out.hex(), miso_bytes=armed.hex(), bits=8 * BLOCK_BYTES,
                            notices={"sck_outside_cs": "ng"}),
                        starts(idle), ends(idle)]
                with ws_run.section(2, name, expect=want):
                    configure_target(target, 0, 0, False)
                    target.arm(BLOCK_BYTES, armed)
                    command = f"blk {how} {BLOCK_HZ} {out.hex()}"
                    ws_run.command(command)
                    dut.write(command)
                    m = wait_reply(dut, wait, rb"BLK ([01]) (-|[0-9a-f]+)")
                    ws_run.reply(m.group(0).decode())
                    if m.group(2) != b"-" and bytes.fromhex(m.group(2).decode()) != armed:
                        wrong.append(f"{name}: read {m.group(2).decode()}, the target was armed with {armed.hex()}")
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (nss, sck, mosi, miso)], rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
        ask(dut, "hwcs 0", rb"HWCS \d \d+")
    assert not wrong, " | ".join(wrong)


SLAVE_HZ = 100_000                 # within what CH32RVSPISlave's README measured (100-200 kHz, one interrupt a byte)
SOFT_SLAVE_HZ = 50_000             # SoftSPISlave: one interrupt a bit, each within half a period (10 us here)
SLAVE_SETUP_NS = 20_000            # CS falling to the first clock: the CS interrupt selects the slave
SLAVE_FILL = 0xA5
PERIPH_BYTES = 4                   # spi.peripheral: the bytes of each transfer


def test_spi_slave(dut, oep_host, ws_run, record_property):
    """spi.slave: CH32RVSPISlave on the SPI's default pins, CS the first other labelled line; softspi.slave:
    SoftSPISlave (the same API, every bit one pin-change interrupt) on SoftSPI's four lines, SCK and CS on different
    pin bits (EXTI lines). The probe's spi-controller is the master at 100 kHz (SoftSPISlave 50 kHz, SOFT_SLAVE_HZ)
    with 20 us from CS to the first clock and from the last clock to CS. One capture of the four lines
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
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    if soft and int(sck[2:]) == int(cs[2:]):
        pytest.skip(f"n/a: SoftSPISlave wants SCK and CS on different EXTI lines, {sck} / {cs} share one")
    hz = SOFT_SLAVE_HZ if soft else SLAVE_HZ
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
    # SoftSPISlave puts MISO up an interrupt's latency after the edge, well clear of it.
    edge_sample = "before" if not soft and case["target"].get("family") in ("CH32V103", "CH32V20x") else None
    wrong = []

    def steps(wait):
        idle = {cs: 1}
        cpol = None
        with ws_run.section(1, f"{'softspi' if soft else 'spi'} slave {sck} {miso} {mosi} {cs}, {hz // 1000} kHz"):
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
                ctrl.configure(mode=mode, bit_order=lsb, clock_hz=hz)   # outside the section, as in test_spi_peripheral
                wait(0.002)
                with ws_run.section(2, name, expect=want):
                    for command, reply in ((f"slave {lines[cs]['pin']} {mode} {lsb} {SLAVE_FILL:02x}", rb"SLAVE (\d)"),
                                           *(((f"sq {size} {armed.hex()}", rb"SQ (\d)"),) if queued else ())):
                        ws_run.command(command)
                        dut.write(command)
                        m = wait_reply(dut, wait, reply)
                        ws_run.reply(m.group(0).decode())
                        assert m.group(1) == b"1", f"{name}: {command} refused"
                    wait(0.01)
                    got = ctrl.transfer([SpiItem(sent, 0, SLAVE_SETUP_NS, SLAVE_SETUP_NS)])[0].rx
                    wait(0.01)
                    ws_run.command("sres")
                    dut.write("sres")
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
        dut.write("send")
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


def test_spi_peripheral(dut, oep_host, ws_run, record_property):
    """spi.peripheral, softspi.peripheral: the bus itself as a peripheral, as ArduinoCore-API defines it
    (SPI_HAS_PERIPHERAL_MODE): beginTransaction(SPISettings(..., SPI_PERIPHERAL)), then transfer() of each byte as the
    master clocks it, endTransaction() - the hardware SPI on its default pins, or SoftSPI on its four lines. There is
    no chip select in that API: the probe's spi-controller drives CS (the DUT's CS pin an input) and the DUT does not
    look at it; the bytes are framed by the transfer() calls, which wait for the clock. The master runs at 100 kHz
    (SoftSPI, which polls SCK: 50 kHz) with 20 us from CS to the first clock, PERIPH_BYTES bytes, the DUT
    already waiting in transfer(). In each of the four modes and both bit orders (as the probe offers): WireSkein's
    spi on the four lines at 40 MS/s (the mode, the clocks, MOSI = what the probe sent, MISO = what the DUT was given);
    what the DUT read and what the probe read are compared here."""
    from oep_client.fixture import SpiController, SpiItem
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    hz = SOFT_SLAVE_HZ if soft else SLAVE_HZ
    host = oep_host.host
    ctrl = SpiController(host)
    core.plan_apply(host, ctrl.assignments(lines[sck]["channel"], lines[mosi]["channel"], lines[miso]["channel"],
                                           lines[cs]["channel"]))
    orders = [0, 1] if ctrl.features & SpiController.FEATURE_LSB_FIRST else [0]
    record_property("orders", json.dumps(orders))
    # The hardware SPI's slave MISO hold at byte boundaries, as in test_spi_slave.
    edge_sample = "before" if not soft and case["target"].get("family") in ("CH32V103", "CH32V20x") else None
    wrong = []

    def steps(wait):
        idle = {cs: 1}
        cpol = None
        with ws_run.section(1, f"{'softspi' if soft else 'spi'} peripheral {sck} {miso} {mosi} {cs}, {hz // 1000} kHz"):
            for mode in range(4):
                for lsb in orders:
                    name = f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST"
                    same_idle = cpol is not None and (mode >> 1) == cpol
                    cpol = mode >> 1
                    sent = secrets.token_bytes(PERIPH_BYTES)
                    given = secrets.token_bytes(PERIPH_BYTES)
                    want = [spi(sck, mosi=mosi, miso=miso, cs=cs, mode=mode, bit_order="lsb" if lsb else "msb",
                                mosi_bytes=sent.hex(), miso_bytes=given.hex(), bits=8 * PERIPH_BYTES,
                                notices={"sck_outside_cs": "ng" if same_idle else 1}, edge_sample=edge_sample),
                            starts(idle), ends(idle)]
                    # The probe set up outside the condition's section: reconfiguring its SPI moved SCK with CS
                    # high (2 edges, 2026-11-15; fixed in the probe after 0.0.56+7bb1a76). Where CPOL changes, the
                    # probe may still move SCK to the new idle level as the transfer starts: one edge allowed.
                    ctrl.configure(mode=mode, bit_order=lsb, clock_hz=hz)
                    wait(0.002)
                    with ws_run.section(2, name, expect=want):
                        command = f"periph {mode} {lsb} {PERIPH_BYTES} {given.hex()}"
                        ws_run.command(command)
                        dut.write(command)
                        ws_run.reply(wait_reply(dut, wait, rb"PERIPH READY").group(0).decode())
                        wait(0.01)
                        got = ctrl.transfer([SpiItem(sent, 0, SLAVE_SETUP_NS, SLAVE_SETUP_NS)])[0].rx
                        m = wait_reply(dut, wait, rb"PERIPH ([0-9a-f]+|-)")
                        ws_run.reply(m.group(0).decode())
                        read = m.group(1).decode()
                        if read != sent.hex():
                            wrong.append(f"{name}: the DUT read {read}, the probe sent {sent.hex()}")
                        if got != given:
                            wrong.append(f"{name}: the probe read {got.hex()}, the DUT was given {given.hex()}")
                        wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi, miso)], rate=SLAVE_RATE, steps=steps)
    finally:
        core.plan_release(host, [ctrl.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)



def crc8(data, poly):
    """The SPI's CRC: CRC-8 MSB first, unreflected, from 0."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def test_spi_crc(dut, oep_host, ws_run, record_property):
    """spi.crc (SPI.setCRC() / transferCRC(), 2026-11-23): the hardware SPI's CRC unit on its default pins, the probe's
    spi-target armed. Eight random bytes and the CRC frame after them, mode 0 at 1 MHz, polynomials 0x07 and 0x31:
    WireSkein judges the bus (spi: MOSI the bytes then their CRC-8 - computed here as the unit does, MSB first from 0 -
    MISO the armed bytes, 72 clocks); compared here: crcSent() is that CRC, crcReceived() the armed ninth byte, and
    transferCRC() true when that byte is the CRC of the eight before it, false when it is not."""
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    if case.get("case") == "softspi":
        pytest.skip("n/a: the CRC is the hardware SPI's")
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    if ask(dut, "crc 07 00", rb"CRC (\d) \S+ \S+ \S+").group(1) == b"2":
        pytest.skip("n/a: the default SPI here has no CRC unit (a SoftSPI)")
    ch = {role: lines[p]["channel"] for role, p in (("sck", sck), ("miso", miso), ("mosi", mosi), ("cs", cs))}
    host = oep_host.host
    target = SpiTarget(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]))
    wrong = []

    def steps(wait):
        idle = {cs: 1}
        with ws_run.section(1, f"spi crc {sck} {miso} {mosi} {cs}"):
            for poly in (0x07, 0x31):
                for good in (True, False):
                    name = f"poly {poly:02x}, the reply's CRC {'right' if good else 'wrong'}"
                    data = secrets.token_bytes(8)
                    reply = secrets.token_bytes(8)
                    rcrc = crc8(reply, poly) ^ (0 if good else 0x5A)
                    armed = reply + bytes([rcrc])
                    want = [spi(sck, mosi=mosi, miso=miso, cs=cs, mode=0, bit_order="msb",
                                mosi_bytes=(data + bytes([crc8(data, poly)])).hex(), miso_bytes=armed.hex(), bits=72,
                                notices={"sck_outside_cs": 1}),
                            starts(idle), ends(idle)]
                    with ws_run.section(2, name, expect=want):
                        configure_target(target, 0, 0, soft)
                        target.arm(9, armed)
                        command = f"crc {poly:02x} {data.hex()}"
                        ws_run.command(command)
                        dut.write(command)
                        m = wait_reply(dut, wait, rb"CRC (\d) ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]+)")
                        ws_run.reply(m.group(0).decode())
                        ok, sent, got, read = int(m.group(1)), int(m.group(2), 16), int(m.group(3), 16), m.group(4).decode()
                        if sent != crc8(data, poly):
                            wrong.append(f"{name}: crcSent() {sent:02x}, the CRC of {data.hex()} is {crc8(data, poly):02x}")
                        if got != rcrc or read != reply.hex():
                            wrong.append(f"{name}: read {read} then {got:02x}, armed {reply.hex()} {rcrc:02x}")
                        if bool(ok) != good:
                            wrong.append(f"{name}: transferCRC() said {'match' if ok else 'mismatch'}")
                        wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi, miso)], rate=RATE, steps=steps)
    finally:
        core.plan_release(host, [target.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)


def test_spi_three_wire(dut, oep_host, ws_run, record_property):
    """spi.three-wire (SPI.setThreeWire() / receive(), 2026-11-23): the hardware SPI's one-line mode on its default
    pins, no target: six random bytes sent on MOSI's line, then four received from it - the line held by the probe's
    pull-up (0xFF) or pull-down (0x00) while the DUT lets it go. WireSkein judges the bus (spi: the six bytes then four of
    the held level on that line, exactly 80 clocks - the receive stops on the byte asked); compared here: receive()'s
    bytes are the held level. At 250 kHz and 1 MHz."""
    from oep_client.fixture import Gpio
    from wireskein.runlog import ends, spi, starts
    case = settings(__file__)
    if case.get("case") == "softspi":
        pytest.skip("n/a: three wires are the hardware SPI's")
    lines, sck, miso, mosi, cs, soft = open_bus(dut, oep_host, case, record_property)
    if ask(dut, "tw 250000 00 0", rb"TW (\d) \S+").group(1) == b"2":
        pytest.skip("not in this build of the spi sketch (a SoftSPI, or V003's 16 KB)")
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, lines[mosi]["channel"])])
    wrong = []

    def steps(wait):
        idle = {cs: 1}
        with ws_run.section(1, f"spi three wires {sck} {mosi} {cs}"):
            for hz in (250_000, 1_000_000):
                for level in (1, 0):
                    name = f"{hz // 1000} kHz, the line pulled {'up' if level else 'down'}"
                    out = secrets.token_bytes(6)
                    held = (b"\xff" if level else b"\x00") * 4
                    gpio.configure(lines[mosi]["channel"], Gpio.INPUT_PULLUP if level else Gpio.INPUT_PULLDOWN)
                    wait(0.005)
                    want = [spi(sck, mosi=mosi, cs=cs, mode=0, bit_order="msb", mosi_bytes=(out + held).hex(), bits=80,
                                notices={"sck_outside_cs": 1}),
                            starts(idle), ends(idle)]
                    with ws_run.section(2, name, expect=want):
                        command = f"tw {hz} {out.hex()} 4"
                        ws_run.command(command)
                        dut.write(command)
                        m = wait_reply(dut, wait, rb"TW (\d) (\S+)")
                        ws_run.reply(m.group(0).decode())
                        if m.group(1) != b"1" or m.group(2).decode() != held.hex():
                            wrong.append(f"{name}: receive() {m.group(1).decode()} {m.group(2).decode()}, the line held {held.hex()}")
                        wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in (cs, sck, mosi)], rate=RATE, steps=steps)
    finally:
        try:
            gpio.configure(lines[mosi]["channel"], Gpio.INPUT)
        except Exception:
            pass
        core.plan_release(host, [gpio.fn])
    record_property("results", json.dumps(wrong) if wrong else "ok")
    assert not wrong, " | ".join(wrong)

I2S_RATE = 32_000           # 16-bit slots: SYSCLK / 32 / 32 kHz is within the divider on every family (H41x's
                            # 400 MHz reaches down to 24.5 kHz); BCLK 1.024 / 2.048 MHz
I2S_CONDITIONS = [          # name, bits, format (0 Philips, 1 MSB, 2 LSB), mono, WireSkein's format, slot
    ("16-bit Philips", 16, 0, 0, "i2s", 16),
    ("32-bit Philips", 32, 0, 0, "i2s", 32),
    ("24-bit MSB-justified", 24, 1, 0, "left", 32),
    ("24-bit LSB-justified", 24, 2, 0, "right", 32),
    ("16-bit mono", 16, 0, 1, "i2s", 16),
]


def i2s_frames(bits, mono, n=8):
    """n frames of random samples: the bytes write() takes, and the (left, right) WireSkein reads back."""
    import random
    out, want = b"", []
    for _ in range(n):
        vals = [random.randrange(-(1 << (bits - 1)), 1 << (bits - 1)) for _ in range(1 if mono else 2)]
        for v in vals:
            out += (v & ((1 << bits) - 1)).to_bytes(bits // 8, "little")
        want.append((vals[0], vals[0]) if mono else tuple(vals))
    return out, want


def test_i2s(dut, oep_host, ws_run, record_property):
    """spi.i2s (the I2S library, 2026-11-23): an I2S output on three labelled lines that are one of the DUT's I2S
    pads (I2SFIND: CK, WS, SD), playing eight random frames over and over from loop() at 32 kHz. One capture of
    BCLK, WS and SD; a WireSkein section per condition judging the frames (i2s: the format, the slot's BCLKs, the
    sample rate within 1 %, the eight frames in a row among those read) - 16- and 32-bit Philips, 24-bit MSB- and
    LSB-justified in 32-bit slots, 16-bit mono (the sample on both slots). Underruns are recorded, not judged."""
    from wireskein.runlog import i2s
    case = settings(__file__)
    if case.get("case") == "softspi":
        pytest.skip("n/a: I2S is the spi case's")
    m = bench_ready.ready(dut, rb" (\S+)", nonce=secrets.token_hex(4))
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    if ask(dut, "i2s 0 0 0 16 32000 0 0 00", rb"I2S (NONE|\d \d+ -?\d+)").group(1) == b"NONE":
        pytest.skip("n/a: the part has no I2S")
    lines = {line["pin"]: line for line in gpio_lines(oep_host, case["target"])}
    m = ask(dut, "i2sfind " + " ".join(str(p) for p in lines), rb"I2SFIND (NONE|\d+ \d+ \d+)")
    if m.group(1) == b"NONE":
        pytest.skip("no three labelled lines are one I2S's CK, WS and SD: " + ", ".join(l["pad"] for l in lines.values()))
    route = tuple(int(n) for n in m.group(1).split())
    ck, ws, sd = (lines[p]["pad"] for p in route)
    record_property("pins", json.dumps({"ck": ck, "ws": ws, "sd": sd}))
    rates = {}

    def steps(wait):
        with ws_run.section(1, f"i2s {ck} {ws} {sd}"):
            for name, bits, fmt, mono, ws_fmt, slot in I2S_CONDITIONS:
                data, want = i2s_frames(bits, mono)
                expect = [i2s(ck, ws, sd, format=ws_fmt, bits=bits, fs_hz=I2S_RATE, slot_bits=slot, samples=want)]
                with ws_run.section(2, name, expect=expect):
                    command = f"i2s {route[0]} {route[1]} {route[2]} {bits} {I2S_RATE} {fmt} {mono} {data.hex()}"
                    ws_run.command(command)
                    dut.write(command)
                    m = wait_reply(dut, wait, rb"I2S (\d) (\d+) (-?\d+)")
                    ws_run.reply(m.group(0).decode())
                    assert m.group(1) == b"1", f"{name}: begin() failed, lastError() {m.group(3).decode()}"
                    rates[name] = int(m.group(2))
                    wait(0.05)
            dut.write("i2stat")
            rates["underruns"] = int(wait_reply(dut, wait, rb"I2STAT (\d+)").group(1))
            dut.write("i2send")
            wait_reply(dut, wait, rb"I2SEND")

    ws_capture.stream(ws_run, oep_host, [lines[p] for p in route], rate=RATE, steps=steps)
    record_property("sample_rates", json.dumps(rates))


def test_i2s_input(dut, oep_host, ws_run, record_property):
    """spi.i2s-input (the I2S library's input, 2026-11-23): the DUT's I2S as a stereo input (Philips) on the pads
    test_i2s found, its SD driven by the probe's PWM - a square wave not locked to the DUT's clock, so each slot reads
    as all ones, all zeros, or (where an edge falls inside it) mixed. The DUT reads 0.1 s of frames with readBytes()
    and counts them; judged here against what the probe made: the frames all read, the left slot's rises from zeros to
    ones as many as the wave's periods in that time (+-2), ones / (ones + zeros) its duty within 0.03, no more mixed
    slots than two a period, no overrun. One capture of the three lines beside it, judged by WireSkein (i2s: the
    DUT's BCLK and WS at the rate, 16 / 32 BCLKs a slot - what SD carries is the probe's)."""
    from oep_client.fixture import Pwm
    from wireskein.runlog import i2s
    case = settings(__file__)
    if case.get("case") == "softspi":
        pytest.skip("n/a: I2S is the spi case's")
    m = bench_ready.ready(dut, rb" (\S+)", nonce=secrets.token_hex(4))
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    if ask(dut, "i2s 0 0 0 16 32000 0 0 00", rb"I2S (NONE|\d \d+ -?\d+)").group(1) == b"NONE":
        pytest.skip("n/a: the part has no I2S")
    lines = {line["pin"]: line for line in gpio_lines(oep_host, case["target"])}
    m = ask(dut, "i2sfind " + " ".join(str(p) for p in lines), rb"I2SFIND (NONE|\d+ \d+ \d+)")
    if m.group(1) == b"NONE":
        pytest.skip("no three labelled lines are one I2S's CK, WS and SD: " + ", ".join(l["pad"] for l in lines.values()))
    route = tuple(int(n) for n in m.group(1).split())
    ck, ws, sd = (lines[p]["pad"] for p in route)
    host = oep_host.host
    pwm = Pwm(host)
    core.plan_apply(host, pwm.assignments(lines[route[2]]["channel"]))
    pwm.start(0, 500, 0)
    results, wrong = {}, []
    frames = I2S_RATE // 10

    def steps(wait):
        with ws_run.section(1, f"i2s input {ck} {ws} {sd}"):
            for bits, hz, duty in ((16, 500, 0.5), (32, 300, 0.25)):
                name = f"{bits}-bit, SD {hz} Hz duty {duty}"
                expect = [i2s(ck, ws, sd, bits=bits, fs_hz=I2S_RATE, slot_bits=bits)]
                with ws_run.section(2, name, expect=expect):
                    made = pwm.start(0, hz, round(duty * Pwm.FULL))
                    wait(0.01)
                    command = f"i2srx {route[0]} {route[1]} {route[2]} {bits} {I2S_RATE} {frames}"
                    ws_run.command(command)
                    dut.write(command)
                    m = wait_reply(dut, wait, rb"I2SRX ([\d ]+)", timeout=5.0)
                    ws_run.reply(m.group(0).decode())
                    ok, rate, got, ones, zeros, mixed, rises, overruns = (int(v) for v in m.group(1).split())
                    periods = got / rate * made.hz if rate else 0
                    results[name] = dict(rate=rate, frames=got, ones=ones, zeros=zeros, mixed=mixed, rises=rises,
                                         overruns=overruns, probe_hz=made.hz, probe_duty=made.duty / Pwm.FULL,
                                         periods=round(periods, 2))
                    if not ok:
                        wrong.append(f"{name}: begin() failed")
                        continue
                    if got != frames:
                        wrong.append(f"{name}: {got} of {frames} frames read")
                    if abs(rises - periods) > 2:
                        wrong.append(f"{name}: {rises} rises in the left slot, the wave has {periods:.1f} periods")
                    if ones + zeros == 0 or abs(ones / (ones + zeros) - made.duty / Pwm.FULL) > 0.03:
                        wrong.append(f"{name}: {ones} slots of ones, {zeros} of zeros against a duty of "
                                     f"{made.duty / Pwm.FULL:.3f}")
                    if mixed > 2 * periods * 2 + 2:
                        wrong.append(f"{name}: {mixed} mixed slots for {periods:.1f} periods")
                    if overruns:
                        wrong.append(f"{name}: {overruns} overruns")
                    wait(0.01)
            dut.write("i2send")                    # the lines let go outside the sections: undriven they float
            wait_reply(dut, wait, rb"I2SEND")

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in route], rate=RATE, steps=steps)
    finally:
        pwm.start(0, 0, 0)
        core.plan_release(host, [pwm.fn])
    record_property("results", json.dumps(results))
    assert not wrong, " | ".join(wrong)
