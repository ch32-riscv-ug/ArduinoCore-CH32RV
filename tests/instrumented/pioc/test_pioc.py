"""pioc: CH32RVPioc and <ch32rv_pioc.inc> on the bench - sketch_support/pioc (pioc.ino, bench_pulses.S).

test_pioc_pulses: the DUT loads bench_pulses into the PIOC with IO0 on the first PIOC IO0 pad the probe labels as a
plain line (`<port>@<pad>`, not a debug pad; skips without one), then for n = 1, 3, 8 sends command n: the program
answers 0x5A (cmd.out), the request is down once that is taken (done() false: SB_INT_REQ falls on the host's write
of cmd.out, not its read), d1 counts the commands, and IO0 shows exactly n pulses (WireSkein `pulses`), low at the
section's start and end; then once more with onDone() (the PIOC's interrupt): the callback runs once with 0x5A.
The pulse width follows the PIOC's clock (d0 = 40 turns of a 3-cycle loop: about 1.2 to
2.5 us at 48 to 100 MHz) and is recorded, not judged."""
import json
import pathlib
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import port_lines
import ws_capture

RATE = 4_000_000            # pulses of a microsecond or more: 4 / us
DELAY_TURNS = 40
COUNTS = (1, 3, 8)


def _pad(number):
    return f"P{'ABCDEFGHIJK'[(number >> 5) - 2]}{number & 31}"


def _expect(dut, wait, pattern, timeout=3.0):
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def _ask(dut, wait, line, reply, timeout=3.0):
    dut.write(line)
    return _expect(dut, wait, reply, timeout)


def _ask_now(dut, line, reply, timeout=5.0):
    dut.write(line)
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def test_pioc_pulses(dut, ws_run, request, record_property):
    from wireskein.runlog import ends, pulses, starts
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    record_property("io0", io0["pad"])
    assert _ask_now(dut, f"pbegin {io0['pin']}", rb"PBEGIN (\d)").group(1) == b"1", \
        f"CH32RVPioc.begin() refused IO0 on {io0['pad']}"
    got = {}

    def steps(wait):
        low = {io0["pad"]: 0}
        with ws_run.section(1, f"pioc IO0 {io0['pad']}"):
            for n in COUNTS:
                with ws_run.section(2, f"{n} pulses", expect=[pulses(io0["pad"], count=n), starts(low), ends(low)]):
                    wait(0.01)
                    command = f"pcmd {n} {DELAY_TURNS}"
                    ws_run.command(command)
                    m = _ask(dut, wait, command, rb"PCMD (-?\d+) (\d+) (\d)")
                    ws_run.reply(m.group(0).decode())
                    got[n] = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [io0], rate=RATE, steps=steps)
        m = _ask_now(dut, f"pirq 2 {DELAY_TURNS}", rb"PIRQ (-?\d+) (\d+) (\d+)")
        got["irq"] = tuple(int(x) for x in m.groups())
    finally:
        _ask_now(dut, "pend", rb"PEND")
    record_property("replies", json.dumps(got))
    want = {n: (0x5A, i + 1, 0) for i, n in enumerate(COUNTS)}
    want["irq"] = (0x5A, 1, len(COUNTS) + 1)
    assert got == want, f"cmd.out and d1 per command: {got}, want {want}"


WS_RATE = 40_000_000        # 25 ns: WS2812's bands are +-150 ns
WS_FRAMES = ((30, 0), (300, 101))     # (bytes, seed): 10 LEDs, then 100 LEDs (both the word-wise fetch and 16-bit count)


def test_pioc_ws2812(dut, ws_run, request, record_property):
    """The PiocWs2812 example's program (ws2812.S, a copy beside pioc.ino): IO0 on the first PIOC pad the probe
    labels as a plain line; n bytes (byte i = i * 37 + seed) loaded into the PIOC's SRAM at 0x400 by loadSram(),
    the timing worked out from clockHz() as the example does, one command: the frame on IO0 must be WS2812's
    (WireSkein `rgb_led` with the bytes: T0H, T1H, the bit period and the bytes themselves), low at the section's
    start and end, and the program answers 0."""
    from wireskein.runlog import ends, rgb_led, starts
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    record_property("io0", io0["pad"])
    got = {}

    def steps(wait):
        low = {io0["pad"]: 0}
        with ws_run.section(1, f"pioc ws2812 IO0 {io0['pad']}"):
            for n, seed in WS_FRAMES:
                data = "".join(f"{(i * 37 + seed) & 0xFF:02x}" for i in range(n))
                with ws_run.section(2, f"{n} bytes", expect=[rgb_led(io0["pad"], data=data), starts(low), ends(low)]):
                    wait(0.01)
                    command = f"pws {io0['pin']} {n} {seed}"
                    ws_run.command(command)
                    m = _ask(dut, wait, command, rb"PWS (\d) (\d) (-?\d+) (\d+) (\d+) (\d+) (\d+)")
                    ws_run.reply(m.group(0).decode())
                    got[n] = [int(x) for x in m.groups()]
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [io0], rate=WS_RATE, steps=steps)
    finally:
        _ask_now(dut, "pend", rb"PEND")
    record_property("replies", json.dumps(got))
    for n, (begun, loaded, status, h0, h1, lo, us) in got.items():
        assert (begun, loaded, status) == (1, 1, 0), f"{n} bytes: begun {begun}, loaded {loaded}, cmd.out {status}"


UART_RATE = 4_000_000       # 4 MS/s: 35 samples a bit at 115200, 4 at 1 Mbaud
UART_SENDS = ((40, 3, 9600), (60, 7, 115200), (60, 11, 1_000_000))   # (bytes, seed, baud)


def test_pioc_uart_tx(dut, ws_run, request, record_property):
    """The PiocUartTx example's program (uart_tx.S, a copy beside pioc.ino): IO0 on the first PIOC pad the probe
    labels as a plain line, begun once (before the capture) and left running; n bytes (byte i = i * 37 + seed), 24 at
    a time through the shared registers as the example sends them, the bit time worked out from clockHz() likewise, at 9600, 115200 and 1 Mbaud: WireSkein's `uart` at the rate the PIOC really
    makes (its clock over the bit's cycles, the sketch reports both) finds exactly those bytes, 8N1, no framing
    error, the line high at the section's start and end; the program answers 0."""
    from wireskein.runlog import ends, starts, uart
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    record_property("io0", io0["pad"])
    # the rate each baud really gets: one probe run first (the sketch reports the clock and the bit's cycles)
    actual = {}
    for n, seed, baud in UART_SENDS:
        m = _ask_now(dut, f"puart {io0['pin']} 1 0 {baud}", rb"PUART (\d) (\d) (-?\d+) (\d+) (\d+)")
        actual[baud] = int(m.group(4)) / int(m.group(5))
    got = {}

    def steps(wait):
        high = {io0["pad"]: 1}
        with ws_run.section(1, f"pioc uart_tx IO0 {io0['pad']}"):
            for n, seed, baud in UART_SENDS:
                data = "".join(f"{(i * 37 + seed) & 0xFF:02x}" for i in range(n))
                with ws_run.section(2, f"{n} bytes at {baud}",
                                    expect=[uart(io0["pad"], actual[baud], data=data), starts(high), ends(high)]):
                    wait(0.01)
                    command = f"puart {io0['pin']} {n} {seed} {baud}"
                    ws_run.command(command)
                    m = _ask(dut, wait, command, rb"PUART (\d) (\d) (-?\d+) (\d+) (\d+)", timeout=5.0)
                    ws_run.reply(m.group(0).decode())
                    got[baud] = [int(x) for x in m.groups()]
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [io0], rate=UART_RATE, steps=steps)
    finally:
        _ask_now(dut, "pend", rb"PEND")
    record_property("replies", json.dumps(got))
    for baud, (begun, loaded, status, hz, cycles) in got.items():
        assert (begun, loaded, status) == (1, 1, 0), f"{baud}: begun {begun}, loaded {loaded}, cmd.out {status}"


NEC_RX_UNIT_HZ = 1_000_000 / 562.5      # the probe's spi-controller clocks one NEC unit (562.5 us) a bit on MOSI
NEC_RX_FRAMES = ((0x00, 0x45), (0x1234, 0x5A), None)    # (address, command); None: a repeat code


def _nec_rx_wave(frame):
    """What an IR receiver module puts out for `frame` (low while the carrier is on), one bit a unit, high around."""
    low, high = [0], [1]
    seq = high * 16 + low * 16
    if frame is None:
        seq += high * 4 + low
    else:
        address, command = frame
        a = [address & 0xFF, (~address & 0xFF) if address <= 0xFF else address >> 8, command, ~command & 0xFF]
        seq += high * 8
        for byte in a:
            for i in range(8):
                seq += low + high * (3 if byte >> i & 1 else 1)
        seq += low
    return seq + high * (-len(seq) % 8 + 16)          # whole bytes, then 9 ms high


def test_pioc_nec_rx(dut, request, record_property):
    """The PiocIrNecRx example's program (nec_rx.S, a copy beside pioc.ino): the probe's spi-controller, its MOSI on
    IO0's line (the first PIOC pad the probe labels as a plain line), clocks out what an IR receiver module puts out
    for an NEC frame - one bit a 562.5 us unit, low while the carrier would be on - while the PIOC waits with the ticks
    the example works out: a plain address (0x00, command 0x45) and an extended one (0x1234, 0x5A) each give
    cmd.out 0 and their 32 bits (the first bit lowest); a repeat code gives 1."""
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and its spi-controller (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    from oep_client import core, host as oh
    from oep_client.fixture import SpiController, SpiItem, pack_wire_bits
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    others = [line for pad, line in lines.items() if pad != io0["pad"]][:3]
    if len(others) < 3:
        pytest.skip("the spi-controller's SCK, MISO and CS want three more plain lines")
    record_property("io0", io0["pad"])
    host = oep_host.host
    pattern = rb"PNECRX (\d) (\d+) (\d+)"
    assert _ask_now(dut, f"pnecrx {io0['pin']}", pattern).group(1) == b"1", "begin() refused nec_rx"
    spi = SpiController(host)
    try:
        core.plan_apply(host, spi.assignments(others[0]["channel"], io0["channel"], others[1]["channel"],
                                              others[2]["channel"]))
    except oh.Rejected:
        pytest.skip("the probe's spi-controller does not take MOSI on IO0's line")
    seen, wrong = {}, []
    try:
        hz = spi.configure(mode=0, bit_order=0, clock_hz=round(NEC_RX_UNIT_HZ))
        record_property("unit_us", round(1e6 / hz, 1))
        spi.transfer([SpiItem(tx=b"\xff" * 4)])              # MOSI high (idle) before the PIOC looks
        for frame in NEC_RX_FRAMES:
            name = "repeat" if frame is None else f"0x{frame[0]:x} 0x{frame[1]:02x}"
            m = _ask_now(dut, f"pnecrx {io0['pin']}", pattern)
            time.sleep(0.005)
            spi.transfer([SpiItem(tx=pack_wire_bits(_nec_rx_wave(frame)))])
            m = _ask_now(dut, "pnecget", rb"PNECGET (-?\d+) ([0-9a-f]{8})", timeout=5)
            status, bits = int(m.group(1)), int(m.group(2), 16)
            seen[name] = dict(status=status, bits=f"{bits:08x}")
            if frame is None:
                want = (1, None)
            else:
                address, command = frame
                second = (~address & 0xFF) if address <= 0xFF else address >> 8
                want = (0, (address & 0xFF) | second << 8 | command << 16 | (~command & 0xFF) << 24)
            if status != want[0] or (want[1] is not None and bits != want[1]):
                wrong.append(f"{name}: cmd.out {status}, bits {bits:08x}"
                             + ("" if want[1] is None else f" (want {want[1]:08x})"))
    finally:
        core.plan_release(host, [spi.fn])
        _ask_now(dut, "pend", rb"PEND")
    record_property("received", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


ONEWIRE_RATE = 4_000_000     # 4 MS/s: 24 samples in the 6 us a 1 or a read slot is held low
ONEWIRE_SENDS = ("cc44", "ccbe", "33")          # Skip ROM + Convert T, Skip ROM + Read Scratchpad, Read ROM
ONEWIRE_READ = 2


def test_pioc_onewire(dut, ws_run, request, record_property):
    """The PiocOneWire example's program (onewire.S, a copy beside pioc.ino) on IO0's line (the first PIOC pad the
    probe labels as a plain line), with no device on it: the probe's GPIO pulls the line up as a 1-Wire bus's
    resistor would. Each section is one command: a reset, the bytes written, 2 read. WireSkein's `onewire`
    (standard speed) finds the reset with no presence pulse and exactly those bytes then ff ff (a read slot with
    nobody answering carries a 1), every slot and low within the standard's limits; the program answers 1 for the
    reset (no presence) and 0 for the write and the read, which give ff ff."""
    from oep_client import core
    from oep_client.fixture import Gpio
    from wireskein.runlog import onewire
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    record_property("io0", io0["pad"])
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, io0["channel"])])
    pattern = rb"POW (\d) (-?\d+) (-?\d+) (-?\d+) ([0-9a-f]*)"
    got = {}
    try:
        gpio.configure(io0["channel"], Gpio.INPUT_PULLUP)
        _ask_now(dut, f"pow {io0['pin']} - 0", pattern)        # begun before the capture: the line let go

        def steps(wait):
            with ws_run.section(1, f"pioc onewire IO0 {io0['pad']}"):
                for sent in ONEWIRE_SENDS:
                    want = [onewire(io0["pad"], data=[sent + "ff" * ONEWIRE_READ], presence=False)]
                    with ws_run.section(2, f"reset, {sent}, read {ONEWIRE_READ}", expect=want):
                        wait(0.01)
                        command = f"pow {io0['pin']} {sent} {ONEWIRE_READ}"
                        ws_run.command(command)
                        m = _ask(dut, wait, command, pattern, timeout=5.0)
                        ws_run.reply(m.group(0).decode())
                        got[sent] = [int(x) for x in m.groups()[:4]] + [m.group(5).decode()]
                        wait(0.01)

        ws_capture.stream(ws_run, oep_host, [io0], rate=ONEWIRE_RATE, steps=steps)
    finally:
        gpio.configure(io0["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])
        _ask_now(dut, "pend", rb"PEND")
    record_property("replies", json.dumps(got))
    for sent, (begun, reset, wrote, read, data) in got.items():
        assert (begun, reset, wrote, read, data) == (1, 1, 0, 0, "ff" * ONEWIRE_READ), \
            f"{sent}: begun {begun}, reset {reset} (1: no presence), write {wrote}, read {read} gave {data}"


SPI_RATE = 20_000_000        # 20 MS/s as the SPI contract: 10 samples in 1 MHz SCK's half period
SPI_SENDS = ((40, 5, 100_000), (60, 9, 1_000_000))   # (bytes, seed, SCK Hz)


def test_pioc_spi_tx(dut, ws_run, request, record_property):
    """The PiocSpiTx example's program (spi_tx.S, a copy beside pioc.ino): SCK on IO0 and MOSI on IO1, the first pair
    of PIOC pads (of one AF or remap value) the probe labels as plain lines, CS a third plain line as a GPIO; begun
    once (before the capture) and left running; n bytes (byte i = i * 37 + seed), 24 at a time as the example sends
    them, at 100 kHz and 1 MHz: WireSkein's `spi` (mode 0, MSB first) finds exactly those bytes within CS, SCK within
    10 % of what the program makes (its clock over 2 * (3 * turns + 6) cycles, the sketch reports both), SCK low and
    CS high at the section's start and end; the program answers 0."""
    from wireskein.runlog import ends, spi, starts
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppadsall", rb"PPADSALL((?: \d+:\d+:\d+)*)")
    pads = [tuple(int(x) for x in t.split(b":")) for t in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADSALL empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    pair = next(((lines[_pad(p0)], lines[_pad(p1)]) for r0, p0, v0 in pads for r1, p1, v1 in pads
                 if r0 == 0 and r1 == 1 and v0 == v1 and _pad(p0) in lines and _pad(p1) in lines), None)
    if pair is None:
        pytest.skip("the probe labels no IO0 and IO1 pads of one route as plain lines")
    sck, mosi = pair
    pioc_pads = {_pad(p) for _, p, _ in pads}
    cs = next((line for pad, line in lines.items() if pad not in pioc_pads), None)
    if cs is None:
        pytest.skip("no third plain line for CS")
    record_property("pads", f"SCK {sck['pad']} MOSI {mosi['pad']} CS {cs['pad']}")
    pattern = rb"PSPI (\d) (-?\d+) (\d+) (\d+)"
    # the SCK each rate really gets: one probe run first (the sketch reports the clock and the half period's turns)
    actual = {}
    for n, seed, hz in SPI_SENDS:
        m = _ask_now(dut, f"pspi {sck['pin']} {mosi['pin']} {cs['pin']} 1 0 {hz}", pattern)
        actual[hz] = int(m.group(3)) / (2 * (3 * int(m.group(4)) + 6))
    record_property("sck_hz", json.dumps(actual))
    got = {}

    def steps(wait):
        idle = {sck["pad"]: 0, cs["pad"]: 1}
        with ws_run.section(1, f"pioc spi_tx SCK {sck['pad']} MOSI {mosi['pad']} CS {cs['pad']}"):
            for n, seed, hz in SPI_SENDS:
                data = "".join(f"{(i * 37 + seed) & 0xFF:02x}" for i in range(n))
                want = [spi(sck["pad"], mosi=mosi["pad"], cs=cs["pad"], mode=0, mosi_bytes=data,
                            freq_hz=actual[hz]), starts(idle), ends(idle)]
                with ws_run.section(2, f"{n} bytes at {hz} Hz", expect=want):
                    wait(0.01)
                    command = f"pspi {sck['pin']} {mosi['pin']} {cs['pin']} {n} {seed} {hz}"
                    ws_run.command(command)
                    m = _ask(dut, wait, command, pattern, timeout=5.0)
                    ws_run.reply(m.group(0).decode())
                    got[hz] = [int(x) for x in m.groups()]
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [cs, sck, mosi], rate=SPI_RATE, steps=steps)
    finally:
        _ask_now(dut, "pend", rb"PEND")
    record_property("replies", json.dumps(got))
    for hz, (begun, status, clock, turns) in got.items():
        assert (begun, status) == (1, 0), f"{hz}: begun {begun}, cmd.out {status}"


NEC_RATE = 4_000_000        # 4 MS/s: about 100 samples a carrier period
NEC_FRAMES = ((0x00, 0x45), (0x1234, 0x5A))     # (address, command): a plain and an extended address


def test_pioc_ir_nec(dut, ws_run, request, record_property):
    """The PiocIrNec example's program (ir_nec.S, a copy beside pioc.ino): IO0 on the first PIOC pad the probe labels
    as a plain line, begun once (before the capture) and left running; one frame per command as the example sends it
    (a plain address, then an extended one): WireSkein's `ir` (NEC, as an IR LED is driven) finds those frames with
    each command's inverse, the carrier within 5 % of what the PIOC makes (its clock over the period's cycles, the
    sketch reports both) and every mark and space within 10 % of NEC's; IO0 low at the section's start and end."""
    from wireskein.runlog import ends, ir, starts
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    record_property("io0", io0["pad"])
    m = _ask_now(dut, f"pnec {io0['pin']} 0 0", rb"PNEC (\d) (-?\d+) (\d+) (\d+)")   # begun before the capture
    carrier = int(m.group(3)) / int(m.group(4))
    got = {}

    def steps(wait):
        low = {io0["pad"]: 0}
        with ws_run.section(1, f"pioc ir_nec IO0 {io0['pad']}"):
            for address, command in NEC_FRAMES:
                frames = [{"addr": address, "cmd": command}]
                with ws_run.section(2, f"NEC {address:#x} {command:#x}",
                                    expect=[ir(io0["pad"], frames=frames, carrier_hz=carrier), starts(low), ends(low)]):
                    wait(0.01)
                    line = f"pnec {io0['pin']} {address} {command}"
                    ws_run.command(line)
                    m = _ask(dut, wait, line, rb"PNEC (\d) (-?\d+) (\d+) (\d+)")
                    ws_run.reply(m.group(0).decode())
                    got[f"{address:#x}/{command:#x}"] = [int(x) for x in m.groups()]
                    wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [io0], rate=NEC_RATE, steps=steps)
    finally:
        _ask_now(dut, "pend", rb"PEND")
    record_property("replies", json.dumps(got))
    record_property("carrier_hz", round(carrier, 1))
    for name, (begun, status, hz, cycles) in got.items():
        assert (begun, status) == (1, 0), f"{name}: begun {begun}, cmd.out {status}"


PULSE_WAVES = ((1_000, 0.25), (10_000, 0.5), (40_000, 0.75))     # (Hz, duty) the probe drives
PULSE_PERIODS = 5


def test_pioc_pulse_in(dut, request, record_property):
    """The PiocPulseIn example's program (pulse_in.S, a copy beside pioc.ino): the probe's PWM fixture drives IO0's
    line (the first PIOC pad the probe labels as a plain line) at 1 kHz 25 %, 10 kHz 50 % and 40 kHz 75 %; the PIOC
    counts five periods' high and low in turns of 8 of its cycles. The median high and low, turned into time by the
    PIOC's clock, within 2 % (and 3 turns: the edges are seen within a turn) of what the probe says it made."""
    from oep_client import core
    from oep_client.fixture import Pwm
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and its PWM fixture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    host = getattr(oep_host, "host", oep_host)
    try:
        pwm = Pwm(host)
    except LookupError:
        pytest.skip("the probe has no PWM fixture")
    record_property("io0", io0["pad"])
    seen, wrong = {}, []
    # the DUT's pad an input before the probe drives it: pulse_in.S makes IO0 one at its start
    assert _ask_now(dut, f"pin {io0['pin']} 0", rb"PIN (\d) (\d+)").group(1) == b"1", "begin() refused pulse_in"
    core.plan_apply(host, [(pwm.fn, 1, io0["channel"])])
    try:
        for hz, duty in PULSE_WAVES:
            made = pwm.start(1, hz, round(duty * 65535))
            real_hz, real_duty = made.hz, made.duty / 65535
            time.sleep(0.05)
            m = _ask_now(dut, f"pin {io0['pin']} {PULSE_PERIODS}", rb"PIN (\d) (\d+)((?: -?\d+:-?\d+)*)")
            clock = int(m.group(2))
            pairs = [tuple(int(x) for x in p.split(":")) for p in m.group(3).decode().split()]
            ok = [p for p in pairs if p[0] >= 0]
            name = f"{hz} Hz {duty:.0%}"
            if len(ok) < PULSE_PERIODS:
                wrong.append(f"{name}: {len(ok)} of {PULSE_PERIODS} periods measured")
                if not ok:
                    continue
            turn_ns = 8e9 / clock
            high = sorted(p[0] for p in ok)[len(ok) // 2] * turn_ns
            low = sorted(p[1] for p in ok)[len(ok) // 2] * turn_ns
            want_high = real_duty / real_hz * 1e9
            want_low = (1 - real_duty) / real_hz * 1e9
            seen[name] = dict(real_hz=round(real_hz, 2), real_duty=round(real_duty, 4), high_ns=round(high),
                              low_ns=round(low), want_high_ns=round(want_high), want_low_ns=round(want_low))
            for what, got, want in (("high", high, want_high), ("low", low, want_low)):
                if abs(got - want) > 0.02 * want + 3 * turn_ns:
                    wrong.append(f"{name}: {what} {got:.0f} ns, the probe made {want:.0f} ns")
    finally:
        pwm.stop(1)
        core.plan_release(host, [pwm.fn])
        _ask_now(dut, "pend", rb"PEND")
    record_property("pulses", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


RX_SENDS = ((9600, 3), (115200, 5), (1_000_000, 9))     # (baud, seed): 24 bytes each


def test_pioc_uart_rx(dut, request, record_property):
    """The PiocUartRx example's program (uart_rx.S, a copy beside pioc.ino): the probe's UART fixture, its TX planned
    on IO0's line (the first PIOC pad the probe labels as a plain line), sends 24 bytes (i * 37 + seed) at 9600,
    115200 and 1 Mbaud while the PIOC waits for them with the waits the example works out: all 24 taken, the same
    bytes, the program answering 0."""
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and its UART fixture (an OEP port)")
    oep_host = request.getfixturevalue("oep_host")
    from oep_client import core, fixture, host as oh, registry
    m = _ask_now(dut, "ppads", rb"PPADS((?: \d+)*)")
    pads = [_pad(int(n)) for n in m.group(1).split()]
    if not pads:
        pytest.skip("the part has no PIOC (PPADS empty)")
    debug = set(target.get("debug_pads") or [])
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)
             if line["role"] is None and line["pad"] not in debug}
    io0 = next((lines[p] for p in pads if p in lines), None)
    if io0 is None:
        pytest.skip("the probe labels none of the PIOC's IO0 pads (" + ", ".join(pads) + ") as a plain line")
    record_property("io0", io0["pad"])
    host = oep_host.host
    # the DUT's pad an input before the probe drives it
    assert _ask_now(dut, f"prx {io0['pin']} 1 9600", rb"PRX (\d) (\d+) (\d+)").group(1) == b"1", "begin() refused uart_rx"
    TX = registry.FIXTURE_UART.enum["role"]["tx"]
    io = fn = None
    for candidate in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(candidate, TX, io0["channel"])])
        except oh.Rejected:
            continue
        io, fn = fixture.FixtureUartIO(host, candidate), candidate
        break
    if io is None:
        pytest.skip("no oep.fixture.uart on the probe takes TX on IO0's line")
    seen, wrong = {}, []
    try:
        for baud, seed in RX_SENDS:
            data = bytes((i * 37 + seed) & 0xFF for i in range(24))
            probe_baud = io.configure(baud)
            time.sleep(0.01)
            m = _ask_now(dut, f"prx {io0['pin']} 24 {baud}", rb"PRX (\d) (\d+) (\d+)")
            time.sleep(0.005)
            io.write(data)
            m = _ask_now(dut, "prxget", rb"PRXGET (-?\d+) (\d+) ([0-9a-f]*)", timeout=5)
            status, got, hexed = int(m.group(1)), int(m.group(2)), m.group(3).decode()
            seen[baud] = dict(probe_baud=probe_baud, status=status, got=got, data=hexed)
            if (status, got, hexed) != (0, 24, data.hex()):
                wrong.append(f"{baud}: cmd.out {status}, {got} of 24 bytes, {hexed} (sent {data.hex()})")
    finally:
        core.plan_release(host, [fn])
        _ask_now(dut, "pend", rb"PEND")
    record_property("received", json.dumps(seen))
    assert not wrong, " | ".join(wrong)
