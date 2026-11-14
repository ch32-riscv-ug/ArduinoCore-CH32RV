"""selfsys (case selfsys): the chip-level calls with no wiring (tests/sketch_support/selfsys/selfsys.ino), beside
selfcheck and selfbus - so they run on a WCH-LinkE's targets too.

selfsys.identity: CH32RV.getUniqueId() is the UID ch32rv read through the debugger (UNIID1's eight digits first, as
ch32rv writes it), getChipModel() names the board's series, and getFlashChipSize() is not 0 where the family has an
electronic signature.

selfsys.temperature: analogReadTemp() reads 0-70 degrees C where the family has a sensor (the bench is indoors); the
factory's calibration word is recorded.

selfsys.restart: CH32RV.restart() boots again with resetReasonName() "software".

selfsys.watchdog: wdtEnable(500) fed for 300 ms, then not fed: the chip resets within 0.15-1.5 s of the last feed
by the host's clock (the LSI's tolerance and the console's latency) and boots with "watchdog".

selfsys.light_sleep: lightSleep() with a timer wake after 200 ms returns CH32RV_SLEEP_OK, wakeupCause() is
CH32RV_WAKEUP_TIMER and millis() moved 150-400 ms across it; a family that cannot sleep says
CH32RV_SLEEP_UNSUPPORTED and skips. deepSleep() is left to the system case: a WCH-LinkE could not get a part out of
standby again (2026-10).

selfsys.tone / servo: on the first free pad (not kept off, not held), tone(1000 Hz) gives 100 periods in 100 ms
(+-3) on the pad's own input path, and a Servo at 1000 / 1500 / 2000 us gives pulseIn() widths within 3 % - the
timers' clocks, tone's toggling, Servo's frame and pulseIn() itself.

selfsys.adc_pads: every bonded pad with an ADC1 channel (not kept off, not held) driven 1 and then 0 as an output,
converted with the pad left as it is, each time right after another channel (the internal reference): at least
80 % of full scale at 1 (30 % on H41x's VIO18 pads, 1.2 V on the board), at most 10 % at 0 - every pad reaches its channel, and a channel that returns the
previous one's value (CH32X035's errata) shows. 80 %: a board's own parts load a pad (UIAPduino's PD3 read 89 %).
A channel the board's record loses ("adc:3", X035's errata lot) is recorded, not judged.

selfsys.rtc: on the RTC families (V20x, V205, V30x, L103 / M103) gettimeofday() moves with the host's clock over
3 s, within 10 % (LSI, measured once against the system clock).

A board whose record loses gpio-drive (CH32M030's motor board) skips tone, servo and adc_pads."""
import json
import pathlib
import secrets
import time

import pytest

RESET = {"software": "software", "watchdog": "watchdog"}
SLEEP_OK, SLEEP_UNSUPPORTED = 0, 2
WAKEUP_TIMER = 1


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def pad(pin):
    return f"P{chr(ord('A') + (pin >> 5) - 2)}{pin & 31}"


def pin_of(name):
    return ((ord(name[1]) - ord("A") + 2) << 5) | int(name[2:])


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


@pytest.fixture
def ready(dut):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    return case["target"]


def test_identity(dut, ready, record_property):
    uid, flash, model = (x.decode() for x in ask(dut, "id", rb"ID (\S+) (\d+) (\S+)").groups())
    record_property("identity", json.dumps(dict(uid=uid, flash=int(flash), model=model)))
    assert ready["fqbn"].split(":")[2].startswith(model) or model in ready.get("chip", ""), \
        f"getChipModel() {model} for a {ready.get('chip')}"
    if uid == "-":
        pytest.skip("no electronic signature on this family")
    assert uid == ready["dut_uid"].lower(), f"getUniqueId() {uid}, ch32rv read {ready['dut_uid']}"
    assert int(flash) > 0


def test_temperature(dut, ready, record_property):
    m = ask(dut, "temp", rb"TEMP (-?\d+) (\S+)")
    t = int(m.group(1))
    if t == -9999:
        pytest.skip("no temperature sensor on this family")
    record_property("temperature", t / 10)
    record_property("temperature_cal", m.group(2).decode())
    assert 0 <= t / 10 <= 70, f"analogReadTemp() {t / 10} C"


def after_reset(dut, timeout=15):
    """The reset reason once the sketch is back: the STARTUP line's arrival (its first bytes may be lost while the
    probe's console comes back after the reset - "watchdog 0" was all of it once on the V103 EVT), then `why`."""
    dut.expect(rb"(?:STARTUP )?\S* ?\d+\r?\n", timeout=timeout)
    t = time.monotonic()
    dut.write("why\n")
    return t, dut.expect(rb"WHY (\S+) (\d+)\r?\n", timeout=5).group(1).decode()


def test_restart(dut, ready):
    dut.write("restart\n")
    dut.expect(rb"RESTARTING\r?\n", timeout=10)
    _, why = after_reset(dut)
    assert why == "software", f"restart() booted with {why}"


def test_watchdog(dut, ready, record_property):
    dut.write("wdt 500\n")
    m = dut.expect(rb"WDT (FED|REFUSED)\r?\n", timeout=10)
    if m.group(1) == b"REFUSED":
        pytest.skip("wdtEnable(500) refused (CH32M030 has no IWDG)")
    dut.expect(rb"WDT LET GO\r?\n", timeout=5)
    t0 = time.monotonic()
    t1, why = after_reset(dut)
    took = t1 - t0
    record_property("watchdog_s", round(took, 3))
    assert why == "watchdog", f"booted with {why} after the watchdog ran out"
    assert 0.15 <= took <= 1.5, f"the reset came {took:.2f} s after the last feed, 500 ms asked"


def free_pad(dut, target):
    for d in target.get("record", {}).get("defects", []):
        if "gpio-drive" in d.get("lose", []):
            pytest.skip(f"the board's pads are not driven: defect {d.get('id')}")
    off = set(target.get("gpio_keep_off") or [])
    import re
    pins = target.get("selfsys_pins") or []
    for name in pins:
        if name in off:
            continue
        if int(ask(dut, f"pad {pin_of(name)}", rb"PAD -?\d+ (\d)").group(1)) == 0:
            return name
    pytest.skip("no free pad among " + ", ".join(pins))


def test_tone(dut, ready, record_property):
    name = free_pad(dut, ready)
    edges, samples = (int(x) for x in ask(dut, f"tone {pin_of(name)} 1000", rb"TONE (\d+) (\d+)").groups())
    record_property("tone", json.dumps(dict(pad=name, edges=edges, samples=samples)))
    assert abs(edges - 100) <= 3, f"tone(1000) on {name}: {edges} periods in 100 ms"


def test_servo(dut, ready, record_property):
    name = free_pad(dut, ready)
    seen, wrong = {}, []
    for us in (1000, 1500, 2000):
        widths = [int(x) for x in ask(dut, f"servo {pin_of(name)} {us}", rb"SERVO (\d+) (\d+) (\d+)").groups()]
        seen[us] = widths
        if any(abs(w - us) > 0.03 * us for w in widths):
            wrong.append(f"{us} us on {name}: pulseIn() read {widths}")
    record_property("servo", json.dumps(seen))
    assert not wrong, " | ".join(wrong)


# CH32H41x's high-speed pads run on VIO18 (internal/ch32rv_vio18.h), 1.2 V on the evaluation board: driven 1 they
# read that, not the full scale.
VIO18_PADS = {f"P{p}{b}" for p, bits in (("A", range(13, 16)), ("B", (0, 1, 10, 11, 12, 13, 14)), ("C", (0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12)),
                                        ("D", (0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15)), ("E", (0, 1, 7, 8, 9, 10, 11, 12, 13, 14, 15)),
                                        ("F", (0, 1, 2, 3, 4, 5, 11, 12, 13, 14))) for b in bits}


def test_adc_pads(dut, ready, record_property):
    for d in ready.get("record", {}).get("defects", []):
        if "gpio-drive" in d.get("lose", []):
            pytest.skip(f"the board's pads are not driven: defect {d.get('id')}")
    off = set(ready.get("gpio_keep_off") or [])
    lost = {f for d in ready.get("record", {}).get("defects", []) for f in d.get("lose", [])}
    pins = [int(p) for p in ask(dut, "adcpins", rb"ADCPINS([ \d]*)").group(1).split()]
    if not pins:
        pytest.skip("no ADC on this board")
    results, wrong, left = {}, [], []
    for pin in pins:
        if pad(pin) in off:
            left.append(f"{pad(pin)} kept off")
            continue
        m = ask(dut, f"adc {pin}", rb"ADC -?\d+ (?:(HELD|NONE)|(\d+) (\d+) (\d+) (\d+))")
        if m.group(1):
            left.append(f"{pad(pin)} {m.group(1).decode().lower()}")
            continue
        channel, high, low, full = (int(x) for x in m.groups()[1:])
        results[pad(pin)] = dict(ch=channel, high=high, low=low)
        if f"adc:{channel}" in lost:
            left.append(f"{pad(pin)} IN{channel}: the board's defect")
            continue
        vio18 = ready.get("chip", "").startswith("CH32H41") and pad(pin) in VIO18_PADS
        if high < (0.3 if vio18 else 0.8) * full or low > 0.1 * full:
            wrong.append(f"{pad(pin)} (IN{channel}): {high} at 1, {low} at 0 of {full}")
    record_property("adc", json.dumps(results))
    record_property("left", json.dumps(left))
    if not results:
        pytest.skip("no ADC pad is free: " + ", ".join(left))
    assert not wrong, " | ".join(wrong)


def test_rtc(dut, ready, record_property):
    m = ask(dut, "rtc", rb"RTC (?:(NONE)|(\d+) (\d+))")
    if m.group(1):
        pytest.skip("no RTC on this family")
    t0 = time.monotonic()
    s0 = int(m.group(2)) + int(m.group(3)) / 1e6
    time.sleep(3)
    m = ask(dut, "rtc", rb"RTC (\d+) (\d+)")
    host = time.monotonic() - t0
    rtc = int(m.group(1)) + int(m.group(2)) / 1e6 - s0
    record_property("rtc", json.dumps(dict(rtc_s=round(rtc, 3), host_s=round(host, 3))))
    assert abs(rtc - host) <= 0.1 * host + 0.1, f"the RTC moved {rtc:.3f} s while the host's clock moved {host:.3f} s"


# Last: a sleep that never ends takes the console with it (the RTC families did, until the alarm-0 fix, 2026-11-12).
def test_light_sleep(dut, ready, record_property):
    t0 = time.monotonic()
    r, cause, took = (int(x) for x in ask(dut, "sleep 200", rb"SLEEP (-?\d+) (-?\d+) (\d+)").groups())
    host = time.monotonic() - t0
    record_property("sleep", json.dumps(dict(result=r, cause=cause, millis=took, host_s=round(host, 3))))
    if r == SLEEP_UNSUPPORTED:
        pytest.skip("lightSleep() unsupported on this family")
    assert r == SLEEP_OK and cause == WAKEUP_TIMER, f"lightSleep() {r}, wakeupCause() {cause}"
    assert 150 <= took <= 400, f"millis() moved {took} ms across a 200 ms sleep"
