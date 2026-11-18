"""selfexti (case selfexti): the EXTI lines that are not pins - CH32RV.attachExtiLine(), enableExtiLineWakeup() and
the PVD (setLowVoltageDetect()) - with no wiring (tests/sketch_support/selfexti/selfexti.ino), beside selfsys and
selfapi. The host knows no line numbers: the DUT lists the lines its series serves (CH32RV_EXTI_LINE_MASK).

selfexti.software_trigger: every line the DUT lists, attached and raised from software (EXTI_SWIEVR): its callback
runs exactly once within 200 us of the trigger, and not at all once detached. 200 us: the interrupt is taken within a
few cycles of the flag; the bound leaves room for a SysTick or console interrupt running first (tens of us at 8 MHz),
not for a wrong vector or a lost flag (no callback in 20 ms).

selfexti.rtc_alarm_line: on the RTC series, attachExtiLine(CH32RV_EXTI_RTC_ALARM) beside setAlarm(time() + 2): both
callbacks run once, the line's 0.95-3.1 s after the call (the RTC's whole second at or after the time asked, as
selfsys.rtc_alarm); after detachExtiLine() setAlarm()'s next alarm still runs its callback (the shared line stays on
for it) and the detached one does not. n/a without an RTC.

selfexti.wake: on the RTC series, the RTC alarm's line as a wake source (enableExtiLineWakeup(), not
enableAlarmWakeup()) ends lightSleep() at setAlarm(time() + 2): CH32RV_SLEEP_OK, CH32RV_WAKEUP_EXTI_LINE, millis()
moved 0.9-3.2 s and the line's callback ran once. The sleep's own alarms are not shown to the callback (a wake on one
would come at a whole second, before 2 s, or say TIMER). n/a without an RTC; the result is recorded where lightSleep()
answers CH32RV_SLEEP_UNSUPPORTED (none since V103 sleeps, 2026-11-18). The AWU families (V00x, X035, M030) have no line the DUT can raise
while it sleeps: their wake on a line is the single (manual) PVD test with a variable supply (TEST_PLAN.ja.md).
A sleeping image: run with CH32_TEST_BOOT_HOLD_MS.

selfexti.pvd: setLowVoltageDetect() at its highest and its lowest threshold, against getVddMillivolts(): a threshold
more than 250 mV above VDD calls back once (at once: VDD is already below it, and no edge follows) and reads
isLowVoltage(); one more than 250 mV below does neither. 250 mV: the thresholds are the datasheet's typical values
(hysteresis 20-180 mV) and VDD is read through the internal reference (a few percent); a threshold within 250 mV of
VDD is recorded, not judged. On a 3.3 V board V003, V103 and X035 have a threshold above VDD; the rest only test the
low side here, and a real fall is the manual test. n/a where setLowVoltageDetect() answers 0 (H41x, M030,
V004 / V007 / M007)."""
import json
import pathlib
import secrets

import pytest

SLEEP_OK, SLEEP_UNSUPPORTED = 0, 2
WAKEUP_EXTI_LINE = 4
MARGIN_MV = 250


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
    return case["target"]


def lines(dut):
    mask, rtc, pvd = ask(dut, "lines", rb"LINES ([0-9a-f]+) (-?\d+) (-?\d+)").groups()
    mask = int(mask, 16)
    return [n for n in range(32) if mask >> n & 1], int(rtc), int(pvd)


def test_software_trigger(dut, ready, record_property):
    served, _, _ = lines(dut)
    if not served:
        pytest.skip("n/a: no EXTI line other than the pins on this series")
    got = {}
    for n in served:
        m = ask(dut, f"swi {n}", rb"SWI (\d+) (\d) (\d+) (\d+) (\d+)")
        got[n] = dict(zip(("attached", "calls", "us", "after_detach"), (int(x) for x in m.groups()[1:])))
    record_property("swi", json.dumps(got))
    wrong = [f"line {n}: {g}" for n, g in got.items()
             if not g["attached"] or g["calls"] != 1 or g["us"] > 200 or g["after_detach"]]
    assert not wrong, "attach, one callback within 200 us of the trigger, none after the detach: " + " | ".join(wrong)


def test_rtc_alarm_line(dut, ready, record_property):
    m = ask(dut, "rtcline 2", rb"RTCLINE (?:(NONE)|(\d+) (\d+) (\d+) (\d+) (\d+))", timeout=15)
    if m.group(1):
        pytest.skip("n/a: no RTC on this series")
    calls, ms, alarms, alarms_after, calls_after = (int(x) for x in m.groups()[1:])
    record_property("rtcline", json.dumps(dict(calls=calls, ms=ms, alarms=alarms, alarms_after=alarms_after,
                                               calls_after=calls_after)))
    assert calls == 1 and alarms == 1, f"line callbacks {calls}, setAlarm() callbacks {alarms} for one alarm"
    assert 950 <= ms <= 3100, f"the line's callback came {ms} ms after setAlarm(time() + 2)"
    assert alarms_after == 1, "setAlarm()'s callback did not run after detachExtiLine() let the line go"
    assert calls_after == 0, f"the detached line's callback ran {calls_after} times"


def test_wake(dut, ready, record_property):
    m = ask(dut, "wake 2", rb"WAKE (?:(NONE)|(-?\d+) (-?\d+) (\d+) (\d+))", timeout=20)
    if m.group(1):
        pytest.skip("n/a: no RTC on this series (no line the DUT can raise while it sleeps)")
    r, cause, slept, calls = (int(x) for x in m.groups()[1:])
    record_property("wake", json.dumps(dict(result=r, cause=cause, millis=slept, calls=calls)))
    if r == SLEEP_UNSUPPORTED:
        pytest.skip("n/a: lightSleep() unsupported on this family (CH32RV_SLEEP_UNSUPPORTED)")
    assert r == SLEEP_OK and cause == WAKEUP_EXTI_LINE, f"lightSleep() {r}, wakeupCause() {cause}"
    assert 900 <= slept <= 3200, f"millis() moved {slept} ms across a sleep to a line 2 s on"
    assert calls == 1, f"the line's callback ran {calls} times across the sleep"


def test_pvd(dut, ready, record_property):
    _, _, line = lines(dut)
    got = {}
    for name, ask_mv in (("high", 99999), ("low", 0)):
        at, vdd, calls, low = (int(x) for x in ask(dut, f"pvd {ask_mv}", rb"PVD (\d+) (\d+) (\d+) (\d)").groups())
        got[name] = dict(threshold=at, vdd=vdd, calls=calls, low=low)
    record_property("pvd", json.dumps(dict(line=line, **got)))
    if got["high"]["threshold"] == 0:
        pytest.skip("n/a: setLowVoltageDetect() answers 0 (no PVD interrupt or no levels in the data)")
    assert line >= 0, "setLowVoltageDetect() set a threshold on a series that lists no PVD line"
    judged = []
    for name, g in got.items():
        if g["vdd"] == 0:
            continue                        # no internal reference to read VDD by
        if g["threshold"] >= g["vdd"] + MARGIN_MV:
            judged.append(name)
            assert g["calls"] == 1 and g["low"] == 1, \
                f"{g['threshold']} mV over VDD {g['vdd']} mV: {g['calls']} callbacks, isLowVoltage() {g['low']}"
        elif g["threshold"] + MARGIN_MV <= g["vdd"]:
            judged.append(name)
            assert g["calls"] == 0 and g["low"] == 0, \
                f"{g['threshold']} mV under VDD {g['vdd']} mV: {g['calls']} callbacks, isLowVoltage() {g['low']}"
    if not judged:
        pytest.skip(f"n/a: every threshold within {MARGIN_MV} mV of VDD ({got})")
