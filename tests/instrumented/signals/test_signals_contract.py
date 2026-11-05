"""signals (case signals): one-line signals on a `<port>@<pad>` line the probe reaches (bench_lines.py gpio_lines). The
test picks the line: for analogWrite() and the pulse capture the first that is a PWM pad of a timer (the DUT says,
from its variant), else the first plain one. Each test skips when the port has no such line.

The waveforms are WireSkein's to judge (pytest-embedded-wireskein's ws_run). One capture of the line streams while
the conditions are gone through (ws_capture.stream); each condition is a section (## <condition>) in which the
command is sent and the line given time to change, and inside it a section (### steady) carrying the expectation,
which WireSkein cuts out of the capture by the probe's clock and checks. The plugin fails the test on an NG or an
unchecked item. What the DUT itself reports (interrupt counts, the capture's readings) is compared here.

- signals.pwm: analogWrite() of 64, 128 and 192 at 1 kHz: a 1 kHz square of that duty (2 %, 1 point); 255 and 0:
  the line high and low.
- signals.servo: Servo.writeMicroseconds() of 544, 1000, 1500, 2000 and 2400 us: a 50 Hz square whose duty is the
  pulse over 20 ms (2 %, 20 us).
- signals.tone: tone() of 440 Hz and 2 kHz: that square at 50 % (1 %, 5 points); after noTone() the line low.
- signals.interrupt: attachInterrupt RISING, FALLING and CHANGE while the probe drives the line through 20 periods:
  20, 20 and 40 edges counted.
- signals.timer-input: CH32RVPulseCapture on the line while the probe's PWM makes 1 kHz at 25 % and 10 kHz at 50 %:
  the period and the high time within 1 % (+ one tick) of what the probe says it made.
- signals.capture-after-pwm: on a PWM pad, CH32RVPulseCapture.begin() is refused while analogWrite() drives it and
  succeeds once the sketch has stopped the PWM in any of the Arduino ways: analogWrite(pin, 0), pinMode(),
  digitalWrite(). How the core shares its timers is its own; this is what a sketch sees."""
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
from wireskein.runlog import level, square


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def ready(dut, case):
    """Restart the DUT, then wait for its answer: each test starts from a fresh run, independent of what an earlier
    test left."""
    dut.write("restart\n")
    # Lines sent while the DUT restarts are lost; ask again until the new run answers.
    for attempt in range(8):
        time.sleep(0.5)
        nonce = secrets.token_hex(4)
        try:
            m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=3)
            break
        except pexpect.TIMEOUT:
            assert attempt < 7, "no answer after the restart"
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"


def labelled_lines(oep_host, case):
    """The port's `<port>@<pad>` lines, or skip."""
    lines = gpio_lines(oep_host, case["target"])
    if not lines:
        pytest.skip("the probe labels no <port>@<pad> line")
    return lines


def first_line(oep_host, case):
    return labelled_lines(oep_host, case)[0]


def timer_pad(dut, lines, channels=(1, 2, 3, 4)):
    """The first of `lines` that is a PWM pad of a timer, on one of `channels` (the DUT's variant says), or None."""
    for candidate in lines:
        m = ask(dut, f"caps {candidate['pin']}", rb"CAPS \d+ (\d+) (\d+)")
        if int(m.group(1)) and int(m.group(2)) in channels:
            return candidate
    return None


def sweep(dut, ws_run, oep_host, line, title, conditions, *, rate=1_000_000):
    """Stream the line while going through `conditions`: (name, command, reply pattern, expectation, settle s, hold s)
    each. The command is sent inside ## name, the line given `settle` to change (the gap after the marker, where it
    may move anywhere), then ### steady holds `hold` with the expectation. Returns the replies."""
    replies = []

    def steps(wait):
        with ws_run.section(1, title):
            for name, command, reply, want, settle, hold in conditions:
                with ws_run.section(2, name):
                    ws_run.command(command)
                    dut.write(command + "\n")
                    wait(settle)
                    # The reply is looked for in short tries with wait() between, so the stream keeps being read
                    # (a long expect would leave it in the probe's buffer).
                    m = None
                    for _ in range(40):
                        try:
                            m = dut.expect(reply + rb"\r?\n", timeout=0.05)
                            break
                        except pexpect.TIMEOUT:
                            wait(0.05)
                    assert m is not None, f"{name}: no reply to {command!r}"
                    ws_run.reply(m.group(0).decode())
                    replies.append(m)
                    with ws_run.section(3, "steady", expect=want):
                        wait(hold)
    ws_capture.stream(ws_run, oep_host, [line], rate=rate, steps=steps)
    return replies


def test_pwm(dut, oep_host, ws_run):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    line = timer_pad(dut, lines)
    if line is None:
        pytest.skip("no labelled line is a PWM pad: " + ", ".join(p["pad"] for p in lines))
    pad, pin = line["pad"], line["pin"]
    conditions = [(f"value={v}", f"pwm {pin} {v} 1000", rb"PWM (\d)",
                   [square(pad, 1000, v / 255, freq_rel=0.02, duty_abs=0.01)] if 0 < v < 255 else [level(pad, 1 if v else 0)],
                   0.02, 0.03)                                     # 30 periods steady
                  for v in (64, 128, 192, 255, 0)]
    try:
        replies = sweep(dut, ws_run, oep_host, line, f"pwm {pad} 1 kHz", conditions)
        assert all(m.group(1) == b"1" for m in replies), f"{pad}: analogWrite() found no PWM there"
    finally:
        ask(dut, f"pwmoff {pin}", rb"PWMOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_servo(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad, pin = line["pad"], line["pin"]
    conditions = [(f"us={us}", f"servo {pin} {us}", rb"SERVO (\d)",
                   [square(pad, 50, us / 20000, freq_rel=0.02, duty_abs=0.001)],
                   0.06, 0.4)                                      # 3 periods to change, 20 steady
                  for us in (544, 1000, 1500, 2000, 2400)]
    try:
        replies = sweep(dut, ws_run, oep_host, line, f"servo {pad}", conditions)
        assert all(m.group(1) == b"1" for m in replies), "attach() failed"
    finally:
        ask(dut, "servooff", rb"SERVOOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_tone(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad, pin = line["pad"], line["pin"]
    conditions = [(f"hz={hz}", f"tone {pin} {hz}", rb"TONE", [square(pad, hz, 0.5, freq_rel=0.01, duty_abs=0.05)],
                   0.02, 0.05) for hz in (440, 2000)]
    conditions.append(("notone", f"notone {pin}", rb"NOTONE", [level(pad, 0)], 0.02, 0.02))
    try:
        sweep(dut, ws_run, oep_host, line, f"tone {pad}", conditions)
    finally:
        ask(dut, f"notone {pin}", rb"NOTONE")
        ask(dut, f"input {pin}", rb"INPUT")


def test_interrupt(dut, oep_host, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, line["channel"])])
    try:
        for mode, name, want in ((1, "rising", 20), (2, "falling", 20), (3, "change", 40)):
            gpio.configure(line["channel"], Gpio.OUTPUT_LOW)
            assert ask(dut, f"irq {line['pin']} {mode}", rb"IRQ (\d)").group(1) == b"1"
            for _ in range(20):
                gpio.configure(line["channel"], Gpio.OUTPUT_HIGH)
                gpio.configure(line["channel"], Gpio.OUTPUT_LOW)
            got = int(ask(dut, "count", rb"COUNT (\d+)").group(1))
            ask(dut, f"irqoff {line['pin']}", rb"IRQOFF")
            record_property(name, got)
            assert got == want, f"{name}: {got} edges counted for {want}"
    finally:
        gpio.configure(line["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])
    record_property("line", line["pad"])


def test_timer_input(dut, oep_host, record_property):
    from oep_client import core
    from oep_client.fixture import Pwm
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    line = timer_pad(dut, lines, channels=(1, 2))
    if line is None:
        pytest.skip("no labelled line is channel 1 or 2 of a timer: " + ", ".join(p["pad"] for p in lines))
    host = oep_host.host
    pwm = Pwm(host)
    core.plan_apply(host, pwm.assignments(line["channel"]))
    try:
        assert ask(dut, f"cap {line['pin']}", rb"CAP (\d)").group(1) == b"1", f"begin() refused {line['pad']}"
        for hz, duty in ((1000, 0.25), (10000, 0.5)):
            made = pwm.start(0, hz, round(duty * Pwm.FULL))
            # read() drops a period that began before the counter came round, so the first one after a change is
            # often refused (CH32RVTimerInput.h): read again until a period is there.
            got = None
            for _ in range(20):
                time.sleep(0.05)
                m = ask(dut, "capread", rb"PULSE (NONE|\d+ \d+ \d+)")
                if m.group(1) != b"NONE":
                    got = m.group(1)
                    break
            assert got is not None, f"no period read in 1 s at {made.hz:.1f} Hz"
            period, high, tick = (int(g) for g in got.split())
            want_period, want_high = tick / made.hz, tick / made.hz * made.duty / Pwm.FULL
            record_property(f"made_{hz}", f"{made.hz:.3f} Hz {made.duty / Pwm.FULL:.4f}")
            record_property(f"read_{hz}", f"{period} {high} @ {tick}")
            assert abs(period - want_period) <= want_period * 0.01 + 1, f"period {period} ticks for {want_period:.0f}"
            assert abs(high - want_high) <= want_period * 0.01 + 1, f"high {high} ticks for {want_high:.0f}"
    finally:
        pwm.stop(0)
        ask(dut, "capoff", rb"CAPOFF")
        core.plan_release(host, [pwm.fn])
    record_property("line", line["pad"])


def test_capture_after_pwm(dut, oep_host, record_property):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    line = timer_pad(dut, lines, channels=(1, 2))
    if line is None:
        pytest.skip("no labelled line is channel 1 or 2 of a timer: " + ", ".join(p["pad"] for p in lines))
    pin = line["pin"]
    seen = {}
    for name, stop in (("analogWrite 0", f"pwm {pin} 0 1000"), ("pinMode", f"input {pin}"),
                       ("digitalWrite", f"level {pin} 0")):
        assert ask(dut, f"pwm {pin} 128 1000", rb"PWM (\d)").group(1) == b"1"
        held = ask(dut, f"cap {pin}", rb"CAP (\d)").group(1)
        ask(dut, "capoff", rb"CAPOFF")
        dut.write(stop + "\n")
        dut.expect(rb"(PWM \d|INPUT|LEVEL)\r?\n", timeout=10)
        back = ask(dut, f"cap {pin}", rb"CAP (\d)").group(1)
        ask(dut, "capoff", rb"CAPOFF")
        seen[name] = (held.decode(), back.decode())
        assert held == b"0", f"{line['pad']}: the capture took the timer while analogWrite() held it"
        assert back == b"1", f"{line['pad']}: CH32RVPulseCapture could not begin after {name} stopped the PWM"
    record_property("refused_then_began", json.dumps(seen))
    ask(dut, f"input {pin}", rb"INPUT")
