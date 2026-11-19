"""system (case system): CH32RV's chip-level calls - the reset reasons, the independent watchdog and the sleeps
(tests/sketch_support/system/system.ino).

- system.reset-reasons: CH32RV.restart() boots with CH32RV_RESET_SOFTWARE, the probe pulling NRST low with
  CH32RV_RESET_EXTERNAL (resetReason(), as the sketch's READY reports it).
- system.watchdog and system.sleep (test_watchdog, test_sleep: a capture each) use two `<port>@<pad>` lines the probe labels (bench_lines.py gpio_lines): the
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
    the timer after 200 ms (the next boot says CH32RV_WAKEUP_TIMER; the pin's high time is judged only where deepSleep()
    is a light sleep and a restart - a real standby lets the pads go and its RTC alarm counts whole seconds). A family that cannot sleep answers
    CH32RV_SLEEP_UNSUPPORTED, and test_sleep is skipped there (the answer recorded); a family whose wdtEnable() refuses
    skips test_watchdog the same way.
- system.window-watchdog (test_window_watchdog, the same two lines and capture): wwdgEnable(T, T / 2) with
  T = min(wwdgMaxTimeoutUs(), 40 ms) - the DUT's own ceiling, asked of it - fed every 3T/4 for 300 ms inside the
  window, then the timing line goes high at the last feed and (late) nothing feeds it, or (early) one more feed
  comes T/8 after it, before the window opens. Either way the next boot says CH32RV_RESET_WINDOW_WATCHDOG (never
  the IWDG's), the early-warning callback ran once before the late reset and never before the early one, and the
  high time is T to one count (a count is at most wwdgMaxTimeoutUs() / 63; the reset never comes after T) or T/8.
- system.backup-registers (test_backup_registers, no lines): every backup word written with a pattern, then
  CH32RV.restart(): the next boot reads all of them back. n/a where backupRegisterCount() is 0 (no RTC domain).
- system.rtc-alarm (test_rtc_alarm, the same two lines and capture): setAlarm(time() + 2 s) with a callback that lets
  the timing line low (high 1-3 s: the alarm is the RTC's whole second at or after the time asked, getAlarm() 0
  after it); lightSleep() with enableAlarmWakeup() (CH32RV_WAKEUP_ALARM, the same high time, millis() across it
  within 20 % of it); deepSleep() the same (the next boot says CH32RV_WAKEUP_ALARM; its high time judged only where
  it is a light sleep and a restart, as for the timer). n/a where setAlarm() refuses (no RTC)."""
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
RESET_POWERON, RESET_EXTERNAL, RESET_SOFTWARE, RESET_WATCHDOG, RESET_WINDOW_WATCHDOG = 1, 2, 3, 4, 5
RESET_DEEPSLEEP = 8           # woke from deepSleep()'s standby: the pads were let go, no width to judge
WAKEUP_TIMER, WAKEUP_PIN, WAKEUP_ALARM = 1, 2, 3
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


def wwdg_tick_us(t_us, max_us):
    """The count the core chose for a timeout: the finest prescaler whose 63 counts hold it (max_us is 63 at /8)."""
    tick = max_us / 63
    for b in range(4):
        tb = tick / 2 ** (3 - b)
        if t_us / tb <= 63:
            return tb
    return tick


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


def test_watchdog(dut, oep_host, ws_run, record_property):
    timed(dut, oep_host, ws_run, record_property, "watchdog")


def test_window_watchdog(dut, oep_host, ws_run, record_property):
    timed(dut, oep_host, ws_run, record_property, "wwdg")


def test_backup_registers(dut, record_property):
    case = settings(__file__)
    boot(dut, case)
    seed = secrets.randbelow(0x10000)
    dut.write(f"bkpw {seed}\n")
    m = expect(dut, time.sleep, rb"BKPW (\d+) (\d)")
    count, keeps = int(m.group(1)), m.group(2) == b"1"
    if count == 0:
        pytest.skip("n/a: backupRegisterCount() is 0 on this family (no RTC domain)")
    dut.write("restart\n")
    time.sleep(0.3)
    reason = boot(dut, case)[0]
    dut.write(f"bkpr {seed}\n")
    m = expect(dut, time.sleep, rb"BKPR (\d+) (\d+)")
    same = int(m.group(2))
    record_property("backup", json.dumps(dict(count=count, keeps_power=keeps, same=same, reason=reason)))
    assert reason == RESET_SOFTWARE, f"after CH32RV.restart() resetReason() is {reason}"
    assert same == count, f"{same} of {count} backup words survived CH32RV.restart()"


def test_rtc_alarm(dut, oep_host, ws_run, record_property):
    timed(dut, oep_host, ws_run, record_property, "alarm")


def test_sleep(dut, oep_host, ws_run, record_property):
    timed(dut, oep_host, ws_run, record_property, "sleep")


def timed(dut, oep_host, ws_run, record_property, part):
    """One capture of the timing and wake lines through `part`'s conditions (watchdog or sleep)."""
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
            if part == "watchdog":
                watchdog(wait, idle)
            elif part == "wwdg":
                wwdg(wait, idle)
            elif part == "alarm":
                alarm(wait, idle)
            else:
                sleep(wait, idle)

    def watchdog(wait, idle):
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

    def wwdg(wait, idle):
        for mode in ("late", "early"):
            name = f"wwdg {mode}"
            with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
                command = f"wwdg {mode} {timing['pin']}"
                ws_run.command(command)
                dut.write(command + "\n")
                m = expect(dut, wait, rb"WWDG (\d+) (\d+) (\d+)")
                t_us, w_us, max_us = (int(x) for x in m.groups())
                try:
                    dut.expect(rb"WWDG REFUSED\r?\n", timeout=0.3)
                    facts[name] = "wwdgEnable() refused"
                    wrong.append(f"{name}: wwdgEnable({t_us}, {w_us}) refused (ceiling {max_us} us)")
                    continue
                except pexpect.TIMEOUT:
                    pass
                wait(0.3 + t_us / 1e6 + 0.3)
                reason, _ = boot(dut, case, wait)
                dut.write("ewi\n")
                ewi, span, steps = (int(x) for x in expect(dut, wait, rb"EWI (\d+) (\d+) (\d+)").groups())
                ws_run.reply(f"reset reason {reason} early warnings {ewi} counter {steps} steps in {span} us")
                facts[name] = dict(timeout_us=t_us, window_us=w_us, max_us=max_us, reason=reason, ewi=ewi,
                                   step_us=round(span / steps, 1) if steps else None)
                # The counter's own rate against the clock the core computed the timeout from.
                want_step = wwdg_tick_us(t_us, max_us)
                if not steps or abs(span / steps / want_step - 1) > 0.15:
                    wrong.append(f"{name}: the counter stepped {steps} times in {span} us, {want_step:.1f} us a step computed")
                if reason != RESET_WINDOW_WATCHDOG:
                    wrong.append(f"{name}: the next boot's resetReason() is {reason}")
                if ewi != (1 if mode == "late" else 0):
                    wrong.append(f"{name}: the early-warning callback ran {ewi} times")
                tick = max_us / 63 / 1e6          # a count at the slowest prescaler: the coarsest it can be
                if mode == "late":
                    expect_high(name, t_us / 1e6 - 2 * tick - 0.0003, t_us / 1e6 + 0.0005)
                else:
                    expect_high(name, t_us / 8e6 * 0.9 - 0.0001, t_us / 8e6 * 1.1 + 0.0005)

    def alarm(wait, idle):
        name = "alarm callback 2 s"
        with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
            command = f"alarm 2 {timing['pin']}"
            ws_run.command(command)
            dut.write(command + "\n")
            if expect(dut, wait, rb"ALARM (\d)").group(1) == b"0":
                facts["alarm"] = "unsupported"
                return
            m = expect(dut, wait, rb"ALARM DONE (\d+) (-?\d+)", timeout=8)
            ws_run.reply(m.group(0).decode())
            took, after = int(m.group(1)), int(m.group(2))
            facts[name] = [took, after]
            if not 950 <= took <= 3100:
                wrong.append(f"{name}: the callback came {took} ms after setAlarm(time() + 2)")
            if after != 0:
                wrong.append(f"{name}: getAlarm() {after} after the alarm ran")
            expect_high(name, 0.95, 3.1)
            wait(0.05)
        name = "lightSleep alarm 2 s"
        with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
            command = f"asleep 2 {timing['pin']}"
            ws_run.command(command)
            dut.write(command + "\n")
            m = expect(dut, wait, rb"ASLEEP (\d+) (\d+) (\d+)", timeout=12)
            ws_run.reply(m.group(0).decode())
            result, cause, slept = (int(x) for x in m.groups())
            facts[name] = [result, cause, slept]
            if (result, cause) != (SLEEP_OK, WAKEUP_ALARM):
                wrong.append(f"{name}: lightSleep() {result}, wakeupCause() {cause}")
            expect_high(name, 0.95, 3.1)
            wait(0.05)
        name = "deepSleep alarm 2 s"
        with ws_run.section(2, name, expect=[starts(idle), ends(idle)]):
            command = f"adeep 2 {timing['pin']}"
            ws_run.command(command)
            dut.write(command + "\n")
            wait(3.5)
            reason, cause = boot(dut, case, wait)
            ws_run.reply(f"reset reason {reason} wakeup cause {cause}")
            facts[name] = [reason, cause]
            if cause != WAKEUP_ALARM:
                wrong.append(f"{name}: the next boot's wakeupCause() is {cause}")
            if reason != RESET_DEEPSLEEP:
                expect_high(name, 0.95, 3.1)
            wait(0.05)

    def sleep(wait, idle):
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
            if reason != RESET_DEEPSLEEP:
                expect_high(name, 0.1, 0.3)
            # else real standby (the RTC families; resetReason() says DEEPSLEEP): the pads are let go as it starts,
            # so the pin is high only until then, and the RTC alarm counts whole seconds - no width to judge (V205
            # 2026-11-13: 0.4 ms). The reset flags it leaves differ (V307 power-on, V203 wake-up only, V103 none):
            # the core tells them apart, not this test.
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
    for section in ("lightSleep timer 200 ms", "lightSleep alarm 2 s"):
        if len(widths.get(section, [])) == 1 and isinstance(facts.get(section), list):
            high, slept = widths[section][0], facts[section][2] / 1000
            if abs(slept / high - 1) > 0.2:
                wrong.append(f"{section}: millis() moved {slept:.3f} s across a {high:.3f} s sleep")
    assert not wrong, " | ".join(wrong)
    if facts.get("sleep") == "unsupported":
        pytest.skip("n/a: lightSleep() answers CH32RV_SLEEP_UNSUPPORTED on this family")
    if part == "watchdog" and all(v == "wdtEnable() refused" for v in facts.values()):
        pytest.skip("n/a: wdtEnable() refuses on this family")
    if part == "alarm" and facts.get("alarm") == "unsupported":
        pytest.skip("n/a: setAlarm() refuses on this family (no RTC)")


MCO_RATE = 100_000_000          # one-shot: 4 samples a period at 25 MHz
MCO_TOP_HZ = 25_000_000         # faster sources would alias at MCO_RATE


def _pad(number):
    return f"P{'ABCDEFGHIJK'[(number >> 5) - 2]}{number & 31}"


def test_clock_output(dut, oep_host, ws_run, record_property):
    """CH32RV.setClockOutput() (MCO): the slowest of the sources the DUT reports (SYSCLK, HSI) that is no faster than
    25 MHz, on the MCO pad when the probe labels it as a plain line - captured once at 100 MS/s and judged by
    WireSkein's `pulses` (the period within 10 % of the source's nominal frequency); then setClockOutput(0) and the
    pad still. Skips where the series has no MCO pad, the probe has no line on it, or every source is too fast."""
    from wireskein.runlog import pulses
    case = settings(__file__)
    boot(dut, case)
    dut.write("mcohz\n")
    m = dut.expect(rb"MCOHZ (\d+)((?: \d+:\d+)*)\r?\n", timeout=5)
    pin = int(m.group(1))
    if not pin:
        pytest.skip("the series has no MCO pad (CH32RV_MCO_PIN)")
    sources = [tuple(int(x) for x in s.split(":")) for s in m.group(2).decode().split()]
    pad = _pad(pin)
    line = next((l for l in gpio_lines(oep_host, case["target"]) if l["pad"].upper() == pad), None)
    if line is None:
        pytest.skip(f"the probe labels no plain line on the MCO pad {pad}")
    usable = sorted((hz, src) for src, hz in sources if hz <= MCO_TOP_HZ)
    if not usable:
        pytest.skip(f"every MCO source is faster than {MCO_TOP_HZ // 1_000_000} MHz: {sources}")
    hz, src = usable[0]
    record_property("mco", json.dumps({"pad": pad, "source": src, "hz": hz}))
    try:
        dut.write(f"mco {src}\n")
        assert dut.expect(rb"MCO (\d)\r?\n", timeout=5).group(1) == b"1", f"setClockOutput({src}) refused"
        time.sleep(0.01)
        with ws_run.section(1, f"MCO {pad} source {src} {hz} Hz",
                            expect=[pulses(pad, period_s=1 / hz, period_rel=0.1)]):
            ws_capture.capture(ws_run, oep_host, [line], rate=MCO_RATE, samples=200_000)
        dut.write("mco 0\n")
        assert dut.expect(rb"MCO (\d)\r?\n", timeout=5).group(1) == b"1", "setClockOutput(0) refused"
        time.sleep(0.01)
        with ws_run.section(1, f"MCO {pad} off", expect=[pulses(pad, count=0)]):
            ws_capture.capture(ws_run, oep_host, [line], rate=1_000_000, samples=10_000)
    finally:
        dut.write("mco 0\n")
