"""selfbus (case selfbus): the buses with no wiring (tests/sketch_support/selfbus/selfbus.ino), selfcheck's sibling.

selfbus.spi_out: the default SPI sends 0xA5 and 0x3C in each of the four modes, MSB and LSB first, at its slowest
clock, while the sketch reads SCK and MOSI on their own pads: SCK idles at the mode's CPOL, 16 edges come, and the
byte rebuilt from MOSI at the mode's sampling edge (leading with CPHA 0, trailing with CPHA 1) is the byte sent -
the SCK / MOSI route, CPOL, CPHA and the bit order.

selfbus.spi_in: MISO made a GPIO output (the SPI still hears the pad) at 0 and at 1: the byte received is 0x00 and
0xFF - the MISO route and the receive path.

selfbus.uart_formats: every HardwareSerial whose default RX pad is free (not kept off, not held - a WCH-Link's TX
on USART1's RX is) receives, at 9600, 8 frames the sketch sends on that pad (made an output) in 8N1, 8E1, 8O1, 7E1
and 8N2: all 8, in order, no error (7E1 reads 7 bits).

selfbus.uart_errors: on the same ports - 8E1 frames with the parity flipped are dropped with PE; 8N1 frames with a 0
stop bit are dropped with FE; all-zero frames come as a break (CH32RV_SERIAL_MARK_BREAK); three frames with
interrupts off throughout raise ORE; more frames than the receive buffer holds, unread, raise
CH32RV_SERIAL_MARK_FULL and leave the buffer full.

selfbus.uart_transmit: every port sends 64 bytes at 115200 in each format: the time is the frame's bits (start,
data, parity, stop) a byte within 4 %.

selfbus.i2c_registers: every hardware TwoWire after setClock(100000) and setClock(400000): CTLR2's FREQ is PCLK1 in
MHz, CKCFGR is PCLK1 / (2 x 100 kHz) in standard mode (4 at least) and FS with PCLK1 / (3 x 400 kHz) in fast mode -
the clock Wire computes from, APB1 divided where HCLK passes FREQ's 60 MHz. Where PCLK1 is still above 60 MHz
(X315, H417, M030 above 48: no bus to divide, TODO) FREQ is recorded, not judged.

selfbus.i2c_nack: a write to 0x50 with nobody on the bus returns an error from endTransmission() within 200 ms -
2 (address NACK) where the board pulls both lines up, any error where they float - and never hangs.

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


FORMATS = {"8N1": 10, "8E1": 11, "8O1": 11, "7E1": 10, "8N2": 11}   # bits a frame
PE, FE, ORE, BREAK, FULL = 0x01, 0x02, 0x08, 0x10, 0x20


@pytest.fixture
def uart(dut):
    no_drive()
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    count, size = (int(x) for x in ask(dut, "ports", rb"PORTS (\d+) (\d+)").groups())
    if not count:
        pytest.skip("no HardwareSerial on this board")
    off = set(case["target"].get("gpio_keep_off") or [])
    fed, left = [], []
    for k in range(count):
        number, pin = (int(x) for x in ask(dut, f"rxpin {k}", rb"RXPIN (\d+) (-?\d+)").groups())
        if pin < 0 or pad(pin) in off:
            left.append(f"USART{number}: {'no default RX' if pin < 0 else pad(pin) + ' kept off'}")
        else:
            fed.append((k, number, pad(pin)))
    return dict(count=count, size=size, fed=fed, left=left)


def rx(dut, k, fmt, kind, n):
    m = ask(dut, f"rxfmt {k} {fmt} {kind} {n}", rb"RXFMT (\d+) (?:(HELD|CLOSED)|(\d+) (\d+) (\d+))", timeout=20)
    if m.group(2):
        return None
    return tuple(int(x) for x in m.groups()[2:])


def test_uart_formats(dut, uart, record_property):
    results, wrong, held = {}, [], []
    for k, number, rx_pad in uart["fed"]:
        for fmt in FORMATS:
            got = rx(dut, k, fmt, "g", 8)
            if got is None:
                held.append(f"USART{number} {rx_pad}")
                break
            received, matching, errors = got
            results[f"USART{number}/{fmt}"] = got
            if (received, matching, errors) != (8, 8, 0):
                wrong.append(f"USART{number} {fmt}: received {received}, {matching} in order of 8, errors {errors:#x}")
    record_property("formats", json.dumps(results))
    record_property("left", json.dumps(uart["left"] + held))
    if not results:
        pytest.skip("no USART's RX pad is free to feed: " + ", ".join(uart["left"] + held))
    assert not wrong, " | ".join(wrong)


def test_uart_errors(dut, uart, record_property):
    results, wrong, held = {}, [], []
    cases = (("8E1", "p", 4, PE, "parity flipped"), ("8N1", "s", 4, FE, "a 0 stop bit"),
             ("8N1", "b", 2, BREAK, "all zero"))
    for k, number, rx_pad in uart["fed"]:
        got = rx(dut, k, "8N1", "o", 3)
        if got is None:
            held.append(f"USART{number} {rx_pad}")
            continue
        results[f"USART{number}/overrun"] = got
        if not got[2] & ORE or got[0] < 1:
            wrong.append(f"USART{number}: 3 frames with interrupts off gave {got[0]} bytes, errors {got[2]:#x} (ORE)")
        for fmt, kind, n, flag, what in cases:
            received, matching, errors = rx(dut, k, fmt, kind, n)
            results[f"USART{number}/{kind}"] = [received, matching, errors]
            if received or not errors & flag:
                wrong.append(f"USART{number} {fmt} {what}: {received} bytes kept, errors {errors:#x} ({flag:#x} expected)")
        n = uart["size"] + 4
        received, matching, errors = rx(dut, k, "8N1", "f", n)
        results[f"USART{number}/full"] = [received, matching, errors]
        if not errors & FULL or received < uart["size"] - 1 or matching != received:
            wrong.append(f"USART{number}: {n} frames unread: {received} kept ({matching} in order) of a "
                         f"{uart['size']}-byte buffer, errors {errors:#x} (FULL expected)")
    record_property("errors", json.dumps(results))
    if not results:
        pytest.skip("no USART's RX pad is free to feed: " + ", ".join(uart["left"] + held))
    assert not wrong, " | ".join(wrong)


def test_uart_transmit(dut, uart, record_property):
    results, wrong = {}, []
    for k in range(uart["count"]):
        for fmt, bits in FORMATS.items():
            m = ask(dut, f"txfmt {k} {fmt} 64", rb"TXFMT (\d+) (?:(CLOSED)|(\d+))", timeout=15)
            if m.group(2):
                break
            number, took = int(m.group(1)), int(m.group(3))
            ideal = 64 * bits * 1e6 / 115200
            results[f"USART{number}/{fmt}"] = took
            if not (0.96 * ideal <= took <= 1.04 * ideal + 200):
                wrong.append(f"USART{number} {fmt}: 64 bytes took {took} us, {ideal:.0f} for {bits} bits a frame")
    record_property("transmit", json.dumps(results))
    assert results, "no port opened"
    assert not wrong, " | ".join(wrong)


@pytest.fixture
def wires(dut):
    no_drive()
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    count = int(ask(dut, "wires", rb"WIRES (\d+)").group(1))
    if not count:
        pytest.skip("no hardware TwoWire on this board")
    off = set(case["target"].get("gpio_keep_off") or [])
    usable, left = [], []
    for k in range(count):
        n, scl, sda, su, sd, du, dd = (int(x) for x in ask(
            dut, f"i2cinfo {k}", rb"I2CINFO (\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)").groups())
        if scl < 0:
            left.append(f"I2C{n}: no default route")
        elif pad(scl) in off or pad(sda) in off:
            left.append(f"I2C{n}: {pad(scl)} / {pad(sda)} kept off")
        elif (su, sd) == (0, 0) or (du, dd) == (0, 0):
            left.append(f"I2C{n}: {pad(scl)} / {pad(sda)} held low by the board")
        else:
            pulled = (su, sd) == (1, 1) and (du, dd) == (1, 1)
            usable.append(dict(k=k, n=n, scl=pad(scl), sda=pad(sda), pulled_up=pulled))
    return dict(usable=usable, left=left)


def test_i2c_registers(dut, wires, record_property):
    if not wires["usable"]:
        pytest.skip("no I2C's pads are free: " + ", ".join(wires["left"]))
    results, wrong = {}, []
    for w in wires["usable"]:
        for hz in (100000, 400000):
            pclk1, freq, ckcfgr = (int(x) for x in ask(dut, f"i2creg {w['k']} {hz}",
                                                       rb"I2CREG (\d+) (\d+) (\d+)").groups())
            results[f"I2C{w['n']}/{hz}"] = dict(pclk1=pclk1, freq=freq, ckcfgr=ckcfgr)
            if pclk1 <= 60_000_000 and freq != pclk1 // 1_000_000:
                wrong.append(f"I2C{w['n']}: FREQ {freq}, PCLK1 is {pclk1 // 1_000_000} MHz")
            if hz == 100000:
                want = max(4, pclk1 // (2 * hz))
                if ckcfgr != want:
                    wrong.append(f"I2C{w['n']} 100 kHz: CKCFGR {ckcfgr:#x}, want {want:#x}")
            else:
                want = 0x8000 | max(1, pclk1 // (3 * hz))
                if ckcfgr & 0x8FFF != want:
                    wrong.append(f"I2C{w['n']} 400 kHz: CKCFGR {ckcfgr:#x}, want FS | {want & 0xFFF:#x}")
    record_property("i2c", json.dumps(results))
    record_property("left", json.dumps(wires["left"]))
    assert not wrong, " | ".join(wrong)


def test_i2c_nack(dut, wires, record_property):
    if not wires["usable"]:
        pytest.skip("no I2C's pads are free: " + ", ".join(wires["left"]))
    results, wrong = {}, []
    for w in wires["usable"]:
        code, took = (int(x) for x in ask(dut, f"i2cnack {w['k']}", rb"I2CNACK (\d+) (\d+)", timeout=15).groups())
        results[f"I2C{w['n']}"] = dict(code=code, us=took, pulled_up=w["pulled_up"])
        if code == 0 or took > 200000:
            wrong.append(f"I2C{w['n']}: endTransmission() {code} after {took} us with nobody on the bus")
        elif w["pulled_up"] and code != 2:
            wrong.append(f"I2C{w['n']}: lines pulled up, endTransmission() {code} (2, an address NACK, expected)")
    record_property("nack", json.dumps(results))
    assert not wrong, " | ".join(wrong)
