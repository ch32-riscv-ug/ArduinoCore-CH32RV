"""selfbus (case selfbus): the buses with no wiring (tests/sketch_support/selfbus/selfbus.ino), selfcheck's sibling.

selfbus.spi_out: the default SPI sends 0xA5 and 0x3C in each of the four modes, MSB and LSB first, at its slowest
clock, while the sketch reads SCK and MOSI on their own pads: SCK idles at the mode's CPOL, 16 edges come, and the
byte rebuilt from MOSI at the mode's sampling edge (leading with CPHA 0, trailing with CPHA 1) is the byte sent -
the SCK / MOSI route, CPOL, CPHA and the bit order.

selfbus.spi_in: MISO made a GPIO output (the SPI still hears the pad) at 0 and at 1: the byte received is 0x00 and
0xFF - the MISO route and the receive path.

Pads kept off (harness/place.py gpio_keep_off) or held by the board are not driven: the test skips. A board whose
record loses gpio-drive (CH32M030's motor board) skips both."""
import json
import pathlib
import secrets

import pytest


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def pad(pin):
    return f"P{chr(ord('A') + (pin >> 5) - 2)}{pin & 31}"


def no_drive():
    for defect in settings(__file__)["target"].get("record", {}).get("defects", []):
        if "gpio-drive" in defect.get("lose", []):
            pytest.skip(f"the board's pads are not driven: defect {defect.get('id')}")


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


@pytest.fixture
def spi(dut):
    no_drive()
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    number, sck, miso, mosi, hs, hi, ho = (int(x) for x in ask(
        dut, "spipins", rb"SPIPINS (\d+) (-?\d+) (-?\d+) (-?\d+) (\d) (\d) (\d)").groups())
    if not number:
        pytest.skip("no hardware SPI with a default route")
    off = set(case["target"].get("gpio_keep_off") or [])
    for name, pin, h in (("SCK", sck, hs), ("MISO", miso, hi), ("MOSI", mosi, ho)):
        if pad(pin) in off:
            pytest.skip(f"SPI{number} {name} {pad(pin)} is kept off")
        if h:
            pytest.skip(f"SPI{number} {name} {pad(pin)} is held by the board")
    return dict(number=number, sck=pad(sck), miso=pad(miso), mosi=pad(mosi))


def test_spi_out(dut, spi, record_property):
    wrong, seen = [], {}
    for mode in range(4):
        for lsb in (0, 1):
            for value in (0xA5, 0x3C):
                idle, edges, got = (int(x) for x in ask(dut, f"spiout {mode} {lsb} {value}",
                                                        rb"SPIOUT (\d) (\d+) (\d+)").groups())
                seen[f"{mode}/{'lsb' if lsb else 'msb'}/{value:02x}"] = [idle, edges, got]
                if idle != mode >> 1:
                    wrong.append(f"mode {mode}: SCK idles at {idle}")
                if edges != 16 or got != value:
                    wrong.append(f"mode {mode} {'LSB' if lsb else 'MSB'} first {value:#04x}: {edges} edges, "
                                 f"MOSI gave {got:#04x}")
    record_property("spi", json.dumps(dict(spi, seen=seen)))
    assert not wrong, " | ".join(wrong)


def test_spi_in(dut, spi, record_property):
    got = {}
    for level in (0, 1):
        m = ask(dut, f"spiin {level}", rb"SPIIN (\d+|HELD)")
        if m.group(1) == b"HELD":
            pytest.skip(f"MISO {spi['miso']} is held by the board")
        got[level] = int(m.group(1))
    record_property("spi_in", json.dumps(got))
    assert got == {0: 0x00, 1: 0xFF}, f"MISO {spi['miso']} at 0 / 1 received {got[0]:#04x} / {got[1]:#04x}"
