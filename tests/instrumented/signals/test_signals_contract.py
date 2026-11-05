"""signals (case signals): one-line signals on a `<port>@<pad>` line the probe reaches (bench_lines.py gpio_lines). The
test picks the line: for analogWrite() and the pulse capture the first that is a PWM pad of a timer (the DUT says,
from its variant), else the first plain one. Each test skips when the port has no such line.

The waveforms are WireSkein's to judge (pytest-embedded-wireskein's ws_run): each condition is a section with its
expectation, the probe's capture of the line is added to it (ws_capture.py), and the plugin fails the test on an NG
or an unchecked item. What the DUT itself reports (interrupt counts, the capture's readings) is compared here.

- signals.pwm: analogWrite() of 64, 128 and 192 at 1 kHz: a 1 kHz square of that duty (2 %, 1 point); 255 and 0:
  the line high and low.
- signals.servo: Servo.writeMicroseconds() of 544, 1000, 1500, 2000 and 2400 us: a 50 Hz square whose duty is the
  pulse over 20 ms (2 %, 20 us).
- signals.tone: tone() of 440 Hz and 2 kHz: that square at 50 % (1 %, 5 points); after noTone() the line low.
- signals.interrupt: attachInterrupt RISING, FALLING and CHANGE while the probe drives the line through 20 periods:
  20, 20 and 40 edges counted.
- signals.timer-input: CH32RVPulseCapture on the line while the probe's PWM makes 1 kHz at 25 % and 10 kHz at 50 %:
  the period and the high time within 1 % (+ one tick) of what the probe says it made."""
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
    """Restart the DUT, then wait for its answer: each test starts from a fresh run (what an earlier test left, as
    analogWrite()'s timer, does not carry over)."""
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


def test_pwm(dut, oep_host, ws_run):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    line = timer_pad(dut, lines)
    if line is None:
        pytest.skip("no labelled line is a PWM pad: " + ", ".join(p["pad"] for p in lines))
    pad = line["pad"]
    try:
        with ws_run.section(1, f"pwm {pad} 1 kHz"):
            for value in (64, 128, 192, 255, 0):
                want = ([square(pad, 1000, value / 255, freq_rel=0.02, duty_abs=0.01)] if 0 < value < 255
                        else [level(pad, 1 if value else 0)])
                with ws_run.section(2, f"value={value}", expect=want):
                    ws_run.command(f"pwm {line['pin']} {value} 1000")
                    ws_run.reply(ask(dut, f"pwm {line['pin']} {value} 1000", rb"PWM (\d)").group(0).decode())
                    time.sleep(0.02)
                    ws_capture.capture(ws_run, oep_host, [line], rate=1_000_000, samples=25_000)   # 25 periods
    finally:
        ask(dut, f"pwmoff {line['pin']}", rb"PWMOFF")
        ask(dut, f"input {line['pin']}", rb"INPUT")


def test_servo(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad = line["pad"]
    try:
        with ws_run.section(1, f"servo {pad}"):
            for us in (544, 1000, 1500, 2000, 2400):
                with ws_run.section(2, f"us={us}", expect=[square(pad, 50, us / 20000, freq_rel=0.02, duty_abs=0.001)]):
                    ws_run.command(f"servo {line['pin']} {us}")
                    m = ask(dut, f"servo {line['pin']} {us}", rb"SERVO (\d)")
                    ws_run.reply(m.group(0).decode())
                    assert m.group(1) == b"1", "attach() failed"
                    time.sleep(0.05)          # the new width from the next period
                    ws_capture.capture(ws_run, oep_host, [line], rate=1_000_000, samples=200_000)   # 10 periods
    finally:
        ask(dut, "servooff", rb"SERVOOFF")
        ask(dut, f"input {line['pin']}", rb"INPUT")


def test_tone(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad = line["pad"]
    try:
        with ws_run.section(1, f"tone {pad}"):
            for hz in (440, 2000):
                with ws_run.section(2, f"hz={hz}", expect=[square(pad, hz, 0.5, freq_rel=0.01, duty_abs=0.05)]):
                    ws_run.command(f"tone {line['pin']} {hz}")
                    ws_run.reply(ask(dut, f"tone {line['pin']} {hz}", rb"TONE").group(0).decode())
                    time.sleep(0.02)
                    ws_capture.capture(ws_run, oep_host, [line], rate=1_000_000, samples=50_000)
            with ws_run.section(2, "notone", expect=[level(pad, 0)]):
                ws_run.command(f"notone {line['pin']}")
                ws_run.reply(ask(dut, f"notone {line['pin']}", rb"NOTONE").group(0).decode())
                ws_capture.capture(ws_run, oep_host, [line], rate=1_000_000, samples=20_000)
    finally:
        ask(dut, f"notone {line['pin']}", rb"NOTONE")
        ask(dut, f"input {line['pin']}", rb"INPUT")


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
