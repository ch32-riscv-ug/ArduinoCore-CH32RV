"""softspi.modes: a SoftSPI on the bench's soft_spi pads ({sck, miso, mosi, cs}, pads of the gpio table) as the
controller of the OEP probe's spi-target (tests/sketch_support/softspi/softspi.ino). In each of the four modes and
both bit orders the sketch sends a few bytes with transfer() and a word with transfer16(), CS low around them.

What must hold: the target receives the bytes, then the word high byte first under MSBFIRST and low byte first
under LSBFIRST (as the hardware SPI and AVR send it), and the sketch reads back what the target was armed with, the
word assembled the same way."""
import json
import pathlib
import secrets

from oep_client import core
from oep_client.fixture import SpiTarget


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def pin(name):
    """The sketch's pin number of a pad name (the variants' CH32RV_PIN(port, bit))."""
    return ((ord(name[1]) - ord("A") + 2) << 5) | int(name[2:])


def test_softspi_contract(dut, oep_host, record_property):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    pads = case["soft_spi"]
    channels = {p["pad"]: p["channel"] for p in case["target"]["gpio"]}
    ch = {role: channels[pads[role]] for role in ("sck", "miso", "mosi", "cs")}
    record_property("pins", json.dumps({r: f"{pads[r]} ch{c}" for r, c in ch.items()}))
    assert ask(dut, f"pins {pin(pads['sck'])} {pin(pads['miso'])} {pin(pads['mosi'])} {pin(pads['cs'])}",
               rb"PINS (\d)").group(1) == b"1", "SoftSPI refused the pins"

    host = oep_host.host
    target = SpiTarget(host)
    core.plan_apply(host, target.assignments(ch["sck"], ch["mosi"], ch["miso"], ch["cs"]))
    try:
        orders = [0, 1] if target.features & SpiTarget.FEATURE_LSB_FIRST else [0]
        record_property("orders", json.dumps(orders))
        for mode in range(4):
            for lsb in orders:
                out = secrets.token_bytes(3)
                word = int.from_bytes(secrets.token_bytes(2), "big")
                miso = secrets.token_bytes(5)
                target.configure(mode=mode, bit_order=lsb)
                target.arm(5, miso)
                m = ask(dut, f"xfer {mode} {lsb} {word:04x} {out.hex()}", rb"XFER (\S+) ([0-9a-f]{4})")
                got_bytes, got_word = bytes.fromhex(m.group(1).decode()), int(m.group(2), 16)
                pending, bits, mosi = target.read_rx()
                wire_word = word.to_bytes(2, "little" if lsb else "big")
                want_word = int.from_bytes(miso[3:5], "little" if lsb else "big")
                label = f"mode {mode} {'LSB' if lsb else 'MSB'}FIRST"
                assert bits == 40, f"{label}: {bits} bits clocked, not 40"
                assert mosi == out + wire_word, f"{label}: the target received {mosi.hex()}, not {(out + wire_word).hex()}"
                assert got_bytes == miso[:3], f"{label}: read {got_bytes.hex()}, the target sent {miso[:3].hex()}"
                assert got_word == want_word, f"{label}: transfer16() read {got_word:04x}, not {want_word:04x}"
    finally:
        core.plan_release(host, [target.fn])
