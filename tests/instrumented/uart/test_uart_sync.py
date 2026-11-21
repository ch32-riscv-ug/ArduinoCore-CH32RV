"""uart-sync: UsartSPI (<UsartSPI.h>), Serial's USART in its synchronous mode, as the controller of the probe's
spi-target - test_uart_synchronous. Its own case and sketch (sketch_support/uart_sync/uart_sync.ino), so that
uart.ino keeps CH32V003's 16 KB."""
import json
import pathlib
import secrets
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import port_lines
import ws_capture


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
    dut.write(line)
    return _expect(dut, wait, reply, timeout)


def _ask_now(dut, line, reply, timeout=5.0):
    dut.write(line)
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def _oep_only(request):
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    if not target.get("upload_port", "").startswith("oep://"):
        pytest.skip("needs the probe's lines and logic capture (an OEP port)")
    return target, request.getfixturevalue("oep_host")


def _serial_lines(oep_host, target):
    roles = {line["role"]: line for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    if set(roles) != {"rx", "tx"}:
        pytest.skip("the probe labels no <port>.rx@<pad> and <port>.tx@<pad>")
    return roles["rx"], roles["tx"]          # the DUT's TX (the probe's rx), the DUT's RX


def _plain_lines(oep_host, target, *taken):
    """The port's `<port>@<pad>` lines (no role), not the chip's debug pads nor a pad already used."""
    debug = set(target.get("debug_pads") or [])
    used = {line["pad"] for line in taken}
    return [line for line in port_lines(oep_host, target)
            if line["role"] is None and line["pad"] not in debug and line["pad"] not in used]


SYNC_RATE = 40_000_000     # 1 MHz SCK's half period 500 ns: 20 / T, as spi.modes
SYNC_CONDITIONS = [(mode, lsb, 250_000) for mode in range(4) for lsb in (0, 1)] + [(0, 0, 1_000_000)]


def _pad(number):
    return f"P{'ABCDEFGHIJK'[(number >> 5) - 2]}{number & 31}"


def test_uart_synchronous(dut, ws_run, request, record_property):
    """UsartSPI (<UsartSPI.h>): Serial's USART in its synchronous mode as the controller of the probe's spi-target -
    CK the route's CK pad (a labelled line of the port, or the test skips), TX MOSI, RX MISO, CS the first plain line
    that is not CK. spi.modes' conditions (the four modes, both bit orders at 250 kHz, mode 0 MSB first at 1 MHz),
    judged as there by WireSkein's `spi` (the mode on the lines, 40 clocks while CS is low, MOSI = the three bytes and
    the word, MISO = the armed bytes, SCK still outside CS but for one move when CPOL changes), and what the DUT read
    compared here. The USART sends least significant bit first: MSBFIRST is the library's reversal. A part without
    the synchronous mode (V00x: no CLKEN) answers USYNC NONE and skips."""
    from oep_client import core
    from oep_client.fixture import SpiTarget
    from wireskein.runlog import ends, spi, starts
    target, oep_host = _oep_only(request)
    dut_tx, dut_rx = _serial_lines(oep_host, target)
    lines = {line["pad"].upper(): line for line in port_lines(oep_host, target)}
    plain = _plain_lines(oep_host, target, dut_tx, dut_rx)
    m = None
    cs = None
    for line in plain:
        m = _ask_now(dut, f"usync {line['pin']}", rb"USYNC (NONE|\d+ \d+ \d+)")
        if m.group(1) == b"NONE":
            pytest.skip("Serial's route has no CK pad, or the part no synchronous mode (USYNC NONE)")
        if _pad(int(m.group(1).split()[0])) != line["pad"].upper():
            cs = line
            break
    if cs is None:
        if m is not None:
            _ask_now(dut, "usync off", rb"USYNC OFF")
        pytest.skip("no plain <port>@<pad> line for CS")
    ck = _pad(int(m.group(1).split()[0]))
    if ck not in lines:
        _ask_now(dut, "usync off", rb"USYNC OFF")
        pytest.skip(f"the probe labels no line on the USART's CK pad {ck}")
    sck, mosi, miso = lines[ck], dut_tx, dut_rx
    record_property("pins", json.dumps({"sck": ck, "miso": miso["pad"], "mosi": mosi["pad"], "cs": cs["pad"]}))
    host = oep_host.host
    spi_target = SpiTarget(host)
    core.plan_apply(host, spi_target.assignments(sck["channel"], mosi["channel"], miso["channel"], cs["channel"]))
    orders = [0, 1] if spi_target.features & SpiTarget.FEATURE_LSB_FIRST else [0]
    read_wrong = []
    clocks = {}

    def steps(wait):
        idle = {cs["pad"]: 1}
        cpol = 0                                       # usync begins in mode 0
        with ws_run.section(1, f"usart spi {ck} {miso['pad']} {mosi['pad']} {cs['pad']}"):
            for mode, lsb, hz in SYNC_CONDITIONS:
                if lsb not in orders:
                    continue
                same_idle = (mode >> 1) == cpol
                cpol = mode >> 1
                name = f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST {hz // 1000} kHz"
                out = secrets.token_bytes(3)
                word = int.from_bytes(secrets.token_bytes(2), "big")
                armed = secrets.token_bytes(5)
                wire_word = word.to_bytes(2, "little" if lsb else "big")
                want = [spi(ck, mosi=mosi["pad"], miso=miso["pad"], cs=cs["pad"], mode=mode,
                            bit_order="lsb" if lsb else "msb", mosi_bytes=(out + wire_word).hex(),
                            miso_bytes=armed.hex(), bits=40, notices={"sck_outside_cs": "ng" if same_idle else 1}),
                        starts(idle), ends(idle)]
                with ws_run.section(2, name, expect=want):
                    spi_target.configure(mode=mode, bit_order=lsb)
                    spi_target.arm(5, armed)
                    command = f"uxfer {mode} {lsb} {hz} {word:04x} {out.hex()}"
                    ws_run.command(command)
                    m = _ask(dut, wait, command, rb"UXFER ([0-9A-Fa-f]*) ([0-9A-Fa-f]{4}) (\d+)")
                    ws_run.reply(m.group(0).decode())
                    clocks[name] = (int(m.group(3)), hz)
                    got_bytes, got_word = bytes.fromhex(m.group(1).decode()), int(m.group(2), 16)
                    want_word = int.from_bytes(armed[3:5], "little" if lsb else "big")
                    if got_bytes != armed[:3] or got_word != want_word:
                        read_wrong.append(f"{name}: read {got_bytes.hex()} {got_word:04x}, "
                                          f"the target was armed with {armed[:3].hex()} {want_word:04x}")
                    wait(0.01)

    try:
        ws_capture.stream(ws_run, oep_host, [cs, sck, mosi, miso], rate=SYNC_RATE, steps=steps)
    finally:
        core.plan_release(host, [spi_target.fn])
        _ask_now(dut, "usync off", rb"USYNC OFF")
    record_property("clocks", json.dumps(clocks))
    record_property("read", json.dumps(read_wrong) if read_wrong else "ok")
    assert all(0 < c <= hz for c, hz in clocks.values()), f"a clock above the one asked for: {clocks}"
    assert not read_wrong, " | ".join(read_wrong)
