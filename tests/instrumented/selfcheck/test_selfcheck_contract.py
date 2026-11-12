"""selfcheck (case selfcheck): what the DUT can tell about itself with nothing wired
(tests/sketch_support/selfcheck/selfcheck.ino) - so it runs on every target, a WCH-LinkE's too.

selfcheck.clocks: the bus clocks the build assumes (CH32RV_PCLK2_HZ, internal/ch32rv_bus_clock.h) are the ones
RCC_CFGR0 is set to, where the family has the STM32F1-shaped PPRE2 field.

selfcheck.serial_transmit: every HardwareSerial object sends 64 bytes at 115200 and 1000000 baud: BRR is the
rounded clock / baud of the USART's own bus (USART1 on PCLK2, the rest on PCLK1), and sending them took 10 bits a
byte at that baud within 4 % (the buffered write, the interrupt and flush() included).

selfcheck.serial_receive: every HardwareSerial object at 9600 baud receives 16 bytes the sketch sends on the
USART's own RX pad, made a GPIO output (the pad's input path, which the USART reads, stays on): all of them, in
order, with no receive error. A 0xFF byte goes first and is dropped: the sketch's bit loop runs slow on its
first pass, which garbled the first byte on V006 and X035 (a frame of ten 1 bits did not warm the LOW write). Not where the pad is kept off (below) or held by the board (a WCH-Link's TX on
USART1's RX: the pulls do not move it, and driving it would fight that output). (Half duplex, the USART hearing its own TX line, received nothing on V203 / V208 /
V307 with TX push-pull, and open drain needs an outside pull-up.) A port whose begin() refuses (no default
route, or the baud out of reach) is reported and skipped.

selfcheck.spi: 128 bytes at 500 kHz on the default SPI: the BR field is the largest divider whose clock does
not exceed 500 kHz from the instance's bus clock, and the transfer took 8 bits a byte at that clock, the
polling's gaps allowed (-2 % .. +25 %).

selfcheck.gpio: on every pad the part bonds, but those kept off (device-data's pinout.csv: the debug pads, NRST
and the BOOT straps, the crystals' pads, USB's D+ / D- and CC - harness/place.py gpio_keep_off): INPUT_PULLUP
reads 1 and INPUT_PULLDOWN 0 - a pad that reads the same with both is held by something on the board, is never
driven and is recorded as `held`; then written HIGH and LOW as an output and read back, and five pulses written
raise 5 RISING, 5 FALLING and 10 CHANGE interrupts (attachInterrupt on the output itself: the EXTI line, its port
select and the dispatch).

selfcheck.pwm: on every PWM pad (a timer channel in the variant's table) but those kept off or held and the DAC
pads (analogWrite() sets the DAC there, analog's subject), analogWrite()
at 64 and 192 of 255, at 1 kHz and 10 kHz, read back on the pad's input path for 50 ms: the rising edges within 2 %
of the periods (2 at least) and the share of samples at 1 within 4 % of the duty - every timer channel's route and
its clock (the APB2 timers keep HCLK when APB2 is halved).

selfcheck.vref: the supply worked out from the internal reference (CH32RV.getVddMillivolts(), with the family's
offset correction) reads 2.7-5.5 V, the parts' operating range (the bench's boards run on 3.3 V, the UIAPduino on
5 V; it read 4432 mV); a family whose data gives no reference voltage (X035) records the raw value and skips.
analogReadVref()'s raw value is recorded only:
on CH32L103 it read 1545 in one run and 945 in the next (2026-11-12), the VDDA / 2 offset the correction takes out.

A board whose record has a defect losing "gpio-drive" (CH32M030's motor board: its pads drive the inverter's
gates) skips every check that drives a pad: serial_receive, spi and gpio.

Started 2026-11-12 with the user's direction: as much as can be checked without wiring, the wiring kept to
what only the outside can show (the pad, the level, the real speed against a partner)."""
import json
import pathlib
import secrets

import pytest


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def pad(pin):
    """The pad name of a sketch pin number (the variants' CH32RV_PIN(port, bit): (port + 2) << 5 | bit)."""
    return f"P{chr(ord('A') + (pin >> 5) - 2)}{pin & 31}"


def no_drive():
    """Skip a check that drives pads where the board's record forbids it (defects' lose: gpio-drive)."""
    for defect in settings(__file__)["target"].get("record", {}).get("defects", []):
        if "gpio-drive" in defect.get("lose", []):
            pytest.skip(f"the board's pads are not driven: defect {defect.get('id')}")


def keep_off():
    """The pads the run says never to drive (harness/place.py gpio_keep_off: debug, reset, straps, crystals, USB)."""
    pads = settings(__file__)["target"].get("gpio_keep_off")
    if pads is None:
        pytest.skip("ch32-device-data is not fetched: no list of the pads to keep off")
    return set(pads)


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
    no_drive()
    count = int(ask(dut, "ports", rb"PORTS (\d+)").group(1))
    if not count:
        pytest.skip("no HardwareSerial on this board")
    off = keep_off()
    n = 16
    results, wrong, left = {}, [], []
    for k in range(count):
        number, pin = (int(x) for x in ask(dut, f"rxpin {k}", rb"RXPIN (\d+) (-?\d+)").groups())
        if pin >= 0 and pad(pin) in off:
            left.append(f"USART{number} ({pad(pin)}: kept off)")
            continue
        m = ask(dut, f"rx {k} {n}", rb"RX (\d+) (?:(CLOSED|HELD)|(\d+) (\d+) (\d+) ([0-9a-f]{8}))", timeout=15)
        if m.group(2):
            left.append(f"USART{number} ({m.group(2).lower()})")
            continue
        received, matching, errors = (int(x) for x in m.groups()[2:5])
        first = m.group(6).decode()
        results[f"USART{number}"] = dict(received=received, matching=matching, errors=errors, first=first)
        if received != n or matching != n or errors:
            wrong.append(f"USART{number}: received {received}, {matching} in order of {n}, errors {errors:#x}, "
                         f"first bytes {first} (sent 55 70 1f 3a)")
    record_property("ports", json.dumps(results))
    if left:
        record_property("left", ", ".join(left))
    assert results, "no port could be fed: " + ", ".join(left)
    assert not wrong, " | ".join(wrong)


def test_spi(dut, ready, record_property):
    no_drive()
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


def test_gpio(dut, ready, record_property):
    no_drive()
    off = keep_off()
    pins = [int(p) for p in ask(dut, "allpins", rb"ALLPINS([ \d]*)").group(1).split()]
    pins = [p for p in pins if pad(p) not in off]
    if not pins:
        pytest.skip("no pad to drive")
    results, wrong, held = {}, [], {}
    for pin in pins:
        m = ask(dut, f"gpio {pin}", rb"GPIO -?\d+ (?:HELD (\d)|(\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+))")
        if m.group(1) is not None:
            held[pad(pin)] = int(m.group(1))     # something on the board holds it: never driven
            continue
        high, low, rise, fall, change, up, down = (int(x) for x in m.groups()[1:])
        results[pad(pin)] = [high, low, rise, fall, change]
        if (high, low) != (1, 0):
            wrong.append(f"{pad(pin)}: read {high} at HIGH, {low} at LOW")
        if (rise, fall, change) != (5, 5, 10):
            wrong.append(f"{pad(pin)}: {rise} RISING, {fall} FALLING, {change} CHANGE of 5, 5, 10")
        if (up, down) != (1, 0):
            wrong.append(f"{pad(pin)}: read {up} with INPUT_PULLUP, {down} with INPUT_PULLDOWN")
    record_property("gpio", json.dumps(results))
    record_property("held", json.dumps(held))
    record_property("kept_off", json.dumps(sorted(off)))
    assert results, "every pad is held or kept off"
    assert not wrong, " | ".join(wrong)


@pytest.mark.parametrize("hz", [1000, 10000])
def test_pwm(dut, ready, hz, record_property):
    no_drive()
    off = keep_off()
    pins = [int(p) for p in ask(dut, "allpins", rb"ALLPINS([ \d]*)").group(1).split()]
    results, wrong, held = {}, [], []
    for pin in (p for p in pins if pad(p) not in off):
        for duty in (64, 192):
            m = ask(dut, f"pwm {pin} {duty} {hz}", rb"PWM -?\d+ (?:(NONE|HELD)|(\d+) (\d+) (\d+) (\d+) (\d+))")
            if m.group(1) == b"NONE":
                break
            if m.group(1) == b"HELD":
                held.append(pad(pin))
                break
            timer, channel, edges, highs, samples = (int(x) for x in m.groups()[1:])
            want_edges = hz * 0.05
            share = highs / samples if samples else 0
            results[f"{pad(pin)}/{duty}"] = dict(tim=f"{timer}.{channel}", edges=edges, duty=round(share, 3),
                                                 samples=samples)
            if abs(edges - want_edges) > max(2, 0.02 * want_edges):
                wrong.append(f"{pad(pin)} (TIM{timer} CH{channel}) at {hz} Hz: {edges} periods in 50 ms, "
                             f"{want_edges:.0f} expected")
            if abs(share - duty / 255) > 0.04:
                wrong.append(f"{pad(pin)} (TIM{timer} CH{channel}) duty {duty}/255 at {hz} Hz: read high "
                             f"{share:.1%} of {samples} samples")
    record_property("pwm", json.dumps(results))
    if held:
        record_property("held", json.dumps(held))
    if not results:
        pytest.skip("no PWM pad to drive")
    assert not wrong, " | ".join(wrong)


def test_vref(dut, ready, record_property):
    value, full, vdd = (int(x) for x in ask(dut, "vref", rb"VREF (-?\d+) (\d+) (\d+)").groups())
    if value < 0 or not full:
        pytest.skip("no ADC")
    if value == 0:
        pytest.skip("no internal reference in the data (CH32M030)")
    record_property("vref", json.dumps(dict(raw=value, full=full, vdd_mv=vdd)))
    if not vdd:
        pytest.skip(f"no reference voltage in the data to work VDD out (raw {value} of {full} recorded)")
    assert 2700 <= vdd <= 5500, f"VDD read as {vdd} mV from the internal reference (raw {value} of {full})"
