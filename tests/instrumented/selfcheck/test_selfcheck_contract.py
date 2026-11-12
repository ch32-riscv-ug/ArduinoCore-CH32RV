"""selfcheck (case selfcheck): what the DUT can tell about itself with nothing wired
(tests/sketch_support/selfcheck/selfcheck.ino) - so it runs on every target, a WCH-LinkE's too.

selfcheck.clocks: the bus clocks the build assumes (CH32RV_PCLK2_HZ, internal/ch32rv_bus_clock.h) are the ones
RCC_CFGR0 is set to, where the family has the STM32F1-shaped PPRE2 field.

selfcheck.serial_transmit: every HardwareSerial object sends 64 bytes at 115200 and 1000000 baud: BRR is the
rounded clock / baud of the USART's own bus (USART1 on PCLK2, the rest on PCLK1), and sending them took 10 bits a
byte at that baud within 4 % (the buffered write, the interrupt and flush() included).

selfcheck.serial_receive: every HardwareSerial object at 9600 baud receives 16 bytes the sketch sends on the
USART's own RX pad, made a GPIO output (the pad's input path, which the USART reads, stays on): all of them, in
order, with no receive error. (Half duplex, the USART hearing its own TX line, received nothing on V203 / V208 /
V307 with TX push-pull, and open drain needs an outside pull-up.) A port whose begin() refuses (no default
route, or the baud out of reach) is reported and skipped.

selfcheck.spi: 128 bytes at 500 kHz on the default SPI: the BR field is the largest divider whose clock does
not exceed 500 kHz from the instance's bus clock, and the transfer took 8 bits a byte at that clock, the
polling's gaps allowed (-2 % .. +25 %).

selfcheck.vref: analogReadVref() reads 1.2 V of a 3.3 V supply within 15 % (the bench's DUTs run on 3.3 V).

Started 2026-11-12 with the user's direction: as much as can be checked without wiring, the wiring kept to
what only the outside can show (the pad, the level, the real speed against a partner)."""
import json
import pathlib
import secrets

import pytest


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


@pytest.fixture
def ready(dut):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    f_cpu, hclk, pclk1, pclk2, cfgr0, from_reg = ask(
        dut, "clocks", rb"CLOCKS (\d+) (\d+) (\d+) (\d+) ([0-9a-f]+) (\d+)").groups()
    return dict(f_cpu=int(f_cpu), hclk=int(hclk), pclk1=int(pclk1), pclk2=int(pclk2), cfgr0=int(cfgr0, 16),
                pclk2_register=int(from_reg))


def test_clocks(ready, record_property):
    record_property("clocks", json.dumps(ready))
    if not ready["pclk2_register"]:
        pytest.skip("the family's RCC has no PPRE2 field this test decodes")
    assert ready["pclk2_register"] == ready["pclk2"], \
        f"RCC_CFGR0 {ready['cfgr0']:#010x} gives PCLK2 {ready['pclk2_register']}, the build assumes {ready['pclk2']}"


@pytest.mark.parametrize("baud", [115200, 1000000])
def test_serial_transmit(dut, ready, baud, record_property):
    count = int(ask(dut, "ports", rb"PORTS (\d+)").group(1))
    if not count:
        pytest.skip("no HardwareSerial on this board")
    n = 64
    results, wrong, closed = {}, [], []
    for k in range(count):
        m = ask(dut, f"tx {k} {baud} {n}", rb"TX (\d+) (?:(CLOSED)|(\d+) (\d+) (\d+))", timeout=15)
        number = int(m.group(1))
        if m.group(2):
            closed.append(f"port {k} (USART{number})")
            continue
        sent, took, brr = (int(x) for x in m.groups()[2:])
        clock = ready["pclk2"] if number == 1 else ready["pclk1"]
        want_brr = (clock + baud // 2) // baud
        ideal = n * 10 * 1e6 / baud
        results[f"USART{number}"] = dict(sent=sent, us=took, brr=brr)
        if sent != n:
            wrong.append(f"USART{number}: sent {sent} of {n}")
        if brr != want_brr:
            wrong.append(f"USART{number}: BRR {brr}, want {want_brr} ({clock} Hz / {baud})")
        if not (0.96 * ideal <= took <= 1.04 * ideal + 200):
            wrong.append(f"USART{number}: {n} bytes took {took} us, {ideal:.0f} at {baud} baud")
    record_property("ports", json.dumps(results))
    if closed:
        record_property("closed", ", ".join(closed))
    assert results, "no port opened"
    assert not wrong, " | ".join(wrong)


def test_serial_receive(dut, ready, record_property):
    count = int(ask(dut, "ports", rb"PORTS (\d+)").group(1))
    if not count:
        pytest.skip("no HardwareSerial on this board")
    n = 16
    results, wrong, closed = {}, [], []
    for k in range(count):
        m = ask(dut, f"rx {k} {n}", rb"RX (\d+) (?:(CLOSED)|(\d+) (\d+) (\d+))", timeout=15)
        number = int(m.group(1))
        if m.group(2):
            closed.append(f"port {k} (USART{number})")
            continue
        received, matching, errors = (int(x) for x in m.groups()[2:])
        results[f"USART{number}"] = dict(received=received, matching=matching, errors=errors)
        if received != n or matching != n or errors:
            wrong.append(f"USART{number}: received {received}, {matching} in order of {n}, errors {errors:#x}")
    record_property("ports", json.dumps(results))
    if closed:
        record_property("closed", ", ".join(closed))
    assert results, "no port opened"
    assert not wrong, " | ".join(wrong)


def test_spi(dut, ready, record_property):
    m = ask(dut, "spi 500000 128", rb"SPI (\d+) (?:(NONE)|(\d+) (\d+))")
    if m.group(2):
        pytest.skip("no hardware SPI with a default route")
    number, took, br = int(m.group(1)), int(m.group(3)), int(m.group(4))
    clock = ready["pclk2"] if number == 1 else ready["pclk1"]
    want_br = next((b for b in range(8) if clock >> (b + 1) <= 500000), 7)
    sck = clock >> (want_br + 1)
    ideal = 128 * 8 * 1e6 / sck
    record_property("spi", json.dumps(dict(spi=number, us=took, br=br, sck=sck)))
    assert br == want_br, f"SPI{number}: BR {br}, want {want_br} ({clock} Hz bus, 500 kHz asked)"
    assert 0.98 * ideal <= took <= 1.25 * ideal, f"SPI{number}: 128 bytes took {took} us, {ideal:.0f} at {sck} Hz"


def test_vref(dut, ready, record_property):
    value, full = (int(x) for x in ask(dut, "vref", rb"VREF (-?\d+) (\d+)").groups())
    if value < 0 or not full:
        pytest.skip("no ADC")
    if value == 0:
        pytest.skip("no internal reference in the data (CH32M030)")
    record_property("vref", value)
    want = 1.2 / 3.3 * full
    assert abs(value - want) <= 0.15 * want, f"analogReadVref() {value} of {full}, about {want:.0f} expected"
