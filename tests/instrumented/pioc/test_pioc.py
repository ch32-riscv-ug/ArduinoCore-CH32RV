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
    dut.write(line + "\n")
    return _expect(dut, wait, reply, timeout)


def _ask_now(dut, line, reply, timeout=5.0):
    dut.write(line + "\n")
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
