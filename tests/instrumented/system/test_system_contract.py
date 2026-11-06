"""system (case system): CH32RV's chip-level calls - the reset reasons, the independent watchdog and the sleeps
(tests/sketch_support/system/system.ino).

- system.reset-reasons: CH32RV.restart() boots with CH32RV_RESET_SOFTWARE, the probe pulling NRST low with
  CH32RV_RESET_EXTERNAL (resetReason(), as the sketch's READY reports it).
- system.watchdog and system.sleep use two `<port>@<pad>` lines the probe labels (bench_lines.py gpio_lines): the
  first is the sketch's timing line, held low by the probe's input pull-down when the DUT lets it go, the second
  the wake line, which the probe drives. One capture of both, a WireSkein section per condition. The timing line is
  high for exactly what is timed: from the watchdog's last feed until its reset lets the line go, or through a
  sleep. Each condition's section expects one whole high pulse of the condition's width there (WireSkein's pulses,
  judged by WireSkein); the widths it measures are recorded (high_s) and the timer sleep's compared with millis().
  - watchdog: wdtEnable(ms) fed every 50 ms for 600 ms (no reset meanwhile: FED comes back), then not fed; the
    high time is the timeout - within -7 % / +3 % of ms on X033 / X035, whose IWDG runs from HSI / 1024 (the
    conversion rounds down, 46 for 46.875 ticks a millisecond), -60 % / +70 % elsewhere (the LSI is specified that
    loosely); the next boot says CH32RV_RESET_WATCHDOG.
  - sleep: lightSleep() woken by the timer after 200 ms (CH32RV_SLEEP_OK, CH32RV_WAKEUP_TIMER, millis() across it
    within 20 % of the high time, the high time within 50 % of 200 ms); lightSleep() woken by the wake line going
    low 200 ms after the sketch says it sleeps (CH32RV_WAKEUP_PIN, the high time 0.15 .. 2 s); deepSleep() woken by
    the timer after 200 ms (the next boot says CH32RV_WAKEUP_TIMER). A family that cannot sleep answers
    CH32RV_SLEEP_UNSUPPORTED, and its sleep conditions are skipped (recorded)."""
import json
import pathlib
import secrets
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import gpio_lines
import ws_capture

# CH32RVResetReason, CH32RVWakeupCause, CH32RVSleepResult (CH32RVSystem.h)
RESET_EXTERNAL, RESET_SOFTWARE, RESET_WATCHDOG = 2, 3, 4
WAKEUP_TIMER, WAKEUP_PIN = 1, 2
SLEEP_OK, SLEEP_UNSUPPORTED = 0, 2


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def boot(dut, case, wait=time.sleep, tries=12):
    """The DUT's answer once it runs (lines sent while it restarts are lost): (reset reason, wakeup cause)."""
    for attempt in range(tries):
        nonce = secrets.token_hex(4)
        dut.write(f"ready {nonce}\n")
        end = time.monotonic() + 1.0
        while time.monotonic() < end:
            try:
                m = dut.expect(rb"READY " + nonce.encode() + rb" (\S+) (-?\d+) (-?\d+)\r?\n", timeout=0.05)
                assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
                return int(m.group(2)), int(m.group(3))
            except pexpect.TIMEOUT:
                wait(0.05)
    raise AssertionError("no answer from the DUT")


def expect(dut, wait, pattern, timeout=5.0):
    end = time.monotonic() + timeout
    while True:
        try:
            return dut.expect(pattern + rb"\r?\n", timeout=0.05)
        except pexpect.TIMEOUT:
            assert time.monotonic() < end, f"no {pattern!r}"
            wait(0.05)


def test_reset_reasons(dut, oep_host, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    case = settings(__file__)
    boot(dut, case)
    dut.write("restart\n")
    time.sleep(0.3)
    software = boot(dut, case)[0]
    nrst = case["target"].get("nrst_channel")
    external = None
    if nrst is not None:
        host = oep_host.host
        gpio = Gpio(host)
        core.plan_apply(host, [(gpio.fn, 1, nrst)])
        try:
            gpio.pulse_low(nrst, 0.02)
        finally:
            core.plan_release(host, [gpio.fn])
        time.sleep(0.3)
        external = boot(dut, case)[0]
    record_property("reasons", json.dumps({"restart": software, "nrst": external}))
    assert software == RESET_SOFTWARE, f"after CH32RV.restart() resetReason() is {software}"
    if nrst is None:
        pytest.skip("the probe labels no <port>.nrst")
    assert external == RESET_EXTERNAL, f"after NRST resetReason() is {external}"


def test_watchdog_and_sleep(dut, oep_host, ws_run, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    from wireskein.runlog import ends, pulses, starts
    from wireskein.verify import check_pulses
    case = settings(__file__)
    lines = gpio_lines(oep_host, case["target"])
    if len(lines) < 2:
        pytest.skip("the watchdog and sleep conditions want two <port>@<pad> lines")
    timing, wake = lines[0], lines[1]
    record_property("lines", json.dumps({"timing": timing["pad"], "wake": wake["pad"]}))
    tight = case["target"].get("family", "") in ("CH32X033", "CH32X035")
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, timing["channel"]), (gpio.fn, 1, wake["channel"])])
    gpio.set([(timing["channel"], Gpio.INPUT_PULLDOWN), (wake["channel"], Gpio.OUTPUT_HIGH)])
    boot(dut, case)
    top = f"system {timing['pad']} {wake['pad']}"
    want = {}          # section name -> its timing pulse (WireSkein's pulses: one whole high, its width bounded)
    facts, wrong = {}, []

    def expect_high(name, lo, hi):
        want[name] = pulses(timing["pad"], count=1, width_min_s=lo, width_max_s=hi)
        ws_run.expect(f"{top}/{name}", [want[name]])

    def steps(wait):
        idle = {timing["pad"]: 0, wake["pad"]: 1}
        with ws_run.section(1, top):
            for ms in (250, 1000):
                name = f"watchdog {ms} ms"
                with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                    command = f"wdt {ms} 50 600 {timing['pin']}"
                    ws_run.command(command)
                    dut.write(command + "\n")
                    ok = expect(dut, wait, rb"WDT (\d)").group(1) == b"1"
                    if not ok:
                        facts[name] = "wdtEnable() refused"
                        continue
                    expect(dut, wait, rb"FED", timeout=3)
                    wait(ms / 1000 * 2 + 0.3)
                    reason, _ = boot(dut, case, wait)
                    ws_run.reply(f"reset reason {reason}")
                    facts[name] = reason
                    if reason != RESET_WATCHDOG:
                        wrong.append(f"{name}: the next boot's resetReason() is {reason}")
                    lo, hi = (0.93, 1.03) if tight else (0.4, 1.7)
                    expect_high(name, ms / 1000 * lo, ms / 1000 * hi)
            name = "lightSleep timer 200 ms"
            with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                command = f"tsleep 200000 {timing['pin']}"
                ws_run.command(command)
                dut.write(command + "\n")
                m = expect(dut, wait, rb"TSLEEP (\d+) (\d+) (\d+)")
                ws_run.reply(m.group(0).decode())
                result, cause, slept = (int(x) for x in m.groups())
                facts[name] = [result, cause, slept]
                if result == SLEEP_UNSUPPORTED:
                    facts["sleep"] = "unsupported"
                    return
                if (result, cause) != (SLEEP_OK, WAKEUP_TIMER):
                    wrong.append(f"{name}: lightSleep() {result}, wakeupCause() {cause}")
                expect_high(name, 0.1, 0.3)
                wait(0.05)
            name = "lightSleep pin"
            with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                command = f"psleep {wake['pin']} {timing['pin']}"
                ws_run.command(command)
                dut.write(command + "\n")
                expect(dut, wait, rb"PSLEEP READY")
                wait(0.2)
                gpio.configure(wake["channel"], Gpio.OUTPUT_LOW)
                m = expect(dut, wait, rb"PSLEEP (\d+) (\d+) (\d+)")
                gpio.configure(wake["channel"], Gpio.OUTPUT_HIGH)
                ws_run.reply(m.group(0).decode())
                result, cause, slept = (int(x) for x in m.groups())
                facts[name] = [result, cause, slept]
                if (result, cause) != (SLEEP_OK, WAKEUP_PIN):
                    wrong.append(f"{name}: lightSleep() {result}, wakeupCause() {cause}")
                expect_high(name, 0.15, 2.0)
                wait(0.05)
            name = "deepSleep timer 200 ms"
            with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                command = f"dsleep 200000 {timing['pin']}"
                ws_run.command(command)
                dut.write(command + "\n")
                wait(0.6)
                reason, cause = boot(dut, case, wait)
                ws_run.reply(f"reset reason {reason} wakeup cause {cause}")
                facts[name] = [reason, cause]
                if cause != WAKEUP_TIMER:
                    wrong.append(f"{name}: the next boot's wakeupCause() is {cause}")
                expect_high(name, 0.1, 0.3)
                wait(0.05)

    try:
        ws_capture.stream(ws_run, oep_host, [timing, wake], rate=1_000_000, steps=steps)
    finally:
        gpio.set([(timing["channel"], Gpio.INPUT), (wake["channel"], Gpio.INPUT)])
        core.plan_release(host, [gpio.fn])
    record_property("facts", json.dumps(facts))
    # The widths WireSkein measured (it judges them through the expectations above); what is compared here is the
    # sketch's own report against them: millis() across the timer sleep.
    widths = {}
    for seg in ws_run.sections():
        section = seg["path"].rsplit("/", 1)[-1]
        if seg["path"] == f"{top}/{section}" and section in want and seg["capture"] is not None:
            _ok, got, _why = check_pulses(seg["capture"], want[section])
            widths[section] = got.get("widths_s", [])
    record_property("high_s", json.dumps({k: [round(w, 4) for w in v] for k, v in widths.items()}))
    section = "lightSleep timer 200 ms"
    if len(widths.get(section, [])) == 1 and isinstance(facts.get(section), list):
        high, slept = widths[section][0], facts[section][2] / 1000
        if abs(slept / high - 1) > 0.2:
            wrong.append(f"{section}: millis() moved {slept:.3f} s across a {high:.3f} s sleep")
    assert not wrong, " | ".join(wrong)
