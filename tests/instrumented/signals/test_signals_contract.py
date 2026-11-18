"""signals (case signals): one-line signals on a `<port>@<pad>` line the probe reaches (bench_lines.py gpio_lines). The
test picks the line: for analogWrite() and the pulse capture the first that is a PWM pad of a timer (the DUT says,
from its variant), else the first plain one. Each test skips when the port has no such line.

The waveforms are WireSkein's to judge (pytest-embedded-wireskein's ws_run). One capture of the line and two
neighbours streams while the test goes through its conditions (ws_capture.stream); each condition is a section (##)
opened as its command is sent, its expectation is the state the line becomes (becomes): before it the state it was
in, then one change with at most `irregular_max` pulses of neither, then every period right. The outer section has
only_moving (the neighbours stay still). The core's rules for a change: a duty or a Servo pulse width switches at
the period's end (0 cut periods), a PWM frequency may cut 2, tone 1, and starting or stopping an output 1 (the
default). The plugin fails the test on an NG or an unchecked item. What the DUT itself reports (interrupt counts,
the capture's readings) is compared here.

- signals.pwm: analogWrite() of 64, 128 and 192 at 1 kHz (a 1 kHz square of that duty, 2 %, 1 point), the same
  value again, 255 and 0 (the line high and low), 128 at 2 kHz and 500 Hz (analogWriteFrequency), and stopped.
- signals.servo: Servo.writeMicroseconds() of 544, 1000, 1500, 2000 and 2400 us and 2400 again (50 Hz, the pulse
  within 30 us), then detach() (low).
- signals.tone: tone() of 440 Hz, 2 kHz and 2 kHz again (50 %, 2 %, 1 point), noTone() (low), and tone() of 1 kHz
  for 30 ms (30 periods, then low).
Each waveform condition wants 20 periods after its change and the change within 300 ms of the command (the console's
path over the network moves by up to about 230 ms).
- signals.interrupt: attachInterrupt RISING, FALLING and CHANGE while the probe drives the line through 20 periods:
  20, 20 and 40 edges counted.
- signals.timer-input: CH32RVPulseCapture on the line while the probe's PWM makes 1 kHz at 25 % and 10 kHz at 50 %:
  the period and the high time within 1 % (+ one tick) of what the probe says it made.
- signals.capture-after-pwm: on a PWM pad, CH32RVPulseCapture.begin() is refused while analogWrite() drives it and
  succeeds once the sketch has stopped the PWM in any of the Arduino ways: analogWrite(pin, 0), pinMode(),
  digitalWrite(). How the core shares its timers is its own; this is what a sketch sees.
- signals.rgb-led: rgbLedWrite() of one LED, the same again, and three LEDs in one ch32rv_rgb_led_write() (one
  frame, no gap between LEDs), captured at 60 MS/s - T = T0H,
  0.35 us: 20 / T is 57 MS/s (tests/README.ja.md, 取り込みの速さ). WireSkein's rgb_led judges the bytes (GRB) and
  every T0H, T1H and bit period within WS2812B's windows (0.25-0.55, 0.65-0.95 and 0.65-1.85 us) and the latch low
  after the frame (50 us at least). On a pad of the WS2812 block (V4x7, X3x5: the DUT says, rgbhw) the frames are
  the block's and judged the same; which way it went is recorded.
- signals.rgb-led-hardware: the same on a pad of the WS2812 block - a `<port>.argb@<pad>` line first, else any
  `<port>@<pad>` line the DUT names as one - with eight LEDs in one call besides three (the DMA feeding the block
  across many bytes). Skips where no labelled line is such a pad. That interrupts stay on while the block sends is
  selfsys.rgb_led_irq's (the DUT counts them itself, no wiring).

The timer features (2026-11-17). The DUT says which ways each line has to a timer (`pad`: its variant's
CH32RV_TIMER_PADS - channel, complementary output or break input, on which route) and what its build holds (`has`:
V003's 16 KB leaves out the timer outputs, the edge capture and Ticker; those tests skip there, naming it). A test
skips, naming what it looked for, when no labelled line is such a pad.
- signals.pwm-route / signals.pwm-complementary: analogWrite() on a pad with no default PWM (CH32RV_PWM_PIN_TO_*)
  that a timer analogWrite() may take (not tone()'s / Servo's TIM4 / TIM5) reaches on another route (a remap, or another alternate function), and on a complementary
  output (CHxN alone: not inverted): 64 and 192 at 1 kHz (2 %, 1 point), stopped. The timer's `route` is recorded.
- signals.complementary: CH32RVComplementaryPwm on a CHx and the CHxN of the same channel and route at 20 kHz, half
  duty, with the dead times 500 ns and 1000 ns: both lines at 20 kHz (10 MS/s), then WireSkein's dead_time between
  them within 100 ns of what the DUT says it set (100 MS/s: T = 100 ns, the tolerance, at 10 / T - tests/README.ja.md
  取り込みの速さ). Where a third line is the timer's break input on that route, the probe holds it high, the DUT
  attaches it active low, the probe pulls it low (both outputs low within 50 ms of the section, the break latched),
  lets go, and resume() runs them again.
- signals.one-pulse: CH32RVOnePulse fired with 200 us delay and 50 us width, then 100 us and 1 ms: exactly one pulse
  each, its width within 2 % + 0.4 us (the DUT's clock; 5 MS/s).
- signals.output-compare: CH32RVOutputCompare with a 1000 us period, TOGGLE 250 us in (a 500 Hz square, half duty),
  TOGGLE moved to 600 us (the same wave, its phase moved), FORCE_ACTIVE (high), FORCE_INACTIVE (low), end (low).
- signals.edge-capture: CH32RVInputCapture at 1 MHz on a channel's pad - CH3 / CH4 or a pad off the default route
  first, when the bench has one - while the probe's PWM makes 1 kHz at 25 %: CHANGE gives the high and low times
  alternately, RISING and FALLING the period, each within 1 % + 1 tick of what the probe made (the first 8 edges).
- signals.timer-pulse-in: timerPulseIn() (CH32RVTimerInput: pulseIn() by the timer's capture, opt-in; pulseIn()
  itself stays the software loop) of HIGH and LOW on a timer channel's pad against the probe's PWM (1 kHz at 25 %,
  10 kHz at 50 %), against the DUT's own timebase measured in the test (micros() against the host's clock):
  within that rate's uncertainty + 3 us (2 ticks and the pads' edges).
- signals.ticker / signals.hw-timer: a Ticker (attach_us 500 and 100) and timerAlarm() (250 us, autoreload)
  toggling a line from their callbacks: 1 kHz, 5 kHz and 2 kHz squares (2 %, half duty within 2 points); once_us
  2000 and a one-shot alarm at 1500 us after the line was set high: one pulse of that width (2 % + 20 us). 1 MS/s:
  the tolerances are tens of microseconds."""
import json
import pathlib
import secrets
import sys
import time

import pexpect
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
import bench_lines
from bench_lines import gpio_lines
import ws_capture
from wireskein.runlog import becomes, dead_time, ends, level, only_moving, pulses, rgb_led, trim


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


# Every waveform condition: 20 whole periods after the change at least (a change that comes late, near the
# section's end, fails), and the change within 300 ms of the command: the window opens as the host writes it, and the
# console's path (host -> probe over the network -> UART) moves by itself - a command's round trip took 8-232 ms on
# v203 and x035 alike (2026-11-17, the tone and pwm sections), a change 124 ms into the window once, so the bound is
# the path's, not the DUT's (the DUT acts within a period of reading the line). Frequencies within 2 % (the DUT's
# internal oscillator, about 1 % by part and temperature).
WAVE = dict(min_periods=20, within_s=0.3)


def sweep(dut, ws_run, oep_host, lines, title, conditions, *, rate=1_000_000, moving=1, quiet=None):
    """Stream `lines` (the lines under test first, `moving` of them) while going through `conditions`: (name, command
    or None, reply pattern, expectations, hold s) each. Each is one section: the command is sent as it opens and the
    line held for `hold`; a callable command is the test's own action (the probe driving a line), with no reply. The
    reply is looked for in short tries with wait() between, so the stream keeps being read. `quiet`: the lines the
    probe's pull-ups hold (the neighbours, lines[moving:], by default). Returns the replies."""
    replies = []
    pads = [line["pad"] for line in lines[:moving]]

    def steps(wait):
        with ws_run.section(1, title, expect=[only_moving(pads)]):
            for name, command, reply, want, hold in conditions:
                with ws_run.section(2, name, expect=want):
                    if callable(command):
                        ws_run.note(name)
                        command()
                    elif command:
                        ws_run.command(command)
                        dut.write(command + "\n")
                        m = None
                        for _ in range(60):
                            try:
                                m = dut.expect(reply + rb"\r?\n", timeout=0.05)
                                break
                            except pexpect.TIMEOUT:
                                wait(0.05)
                        assert m is not None, f"{name}: no reply to {command!r}"
                        ws_run.reply(m.group(0).decode())
                        replies.append(m)
                    wait(hold)
    # The neighbours are only watched (only_moving): the probe's pull-ups hold them, or an undriven pad floats and
    # its drift reads as movement (V203 PA3 next to PA1's PWM, 2026-11-13).
    from oep_client import core
    from oep_client.fixture import Gpio
    host = oep_host.host
    gpio = Gpio(host)
    others = [line["channel"] for line in (lines[moving:] if quiet is None else quiet)]
    if others:
        core.plan_apply(host, [(gpio.fn, 1, ch) for ch in others])
    try:
        for ch in others:
            gpio.configure(ch, Gpio.INPUT_PULLUP)
        ws_capture.stream(ws_run, oep_host, lines, rate=rate, steps=steps)
    finally:
        for ch in others:
            gpio.configure(ch, Gpio.INPUT)
        if others:
            core.plan_release(host, [gpio.fn])
    return replies


def with_neighbours(oep_host, case, line, n=2):
    """The line under test and up to n other labelled lines, for only_moving."""
    return [line] + [p for p in gpio_lines(oep_host, case["target"]) if p["pad"] != line["pad"]][:n]


def test_pwm(dut, oep_host, ws_run):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    line = timer_pad(dut, lines)
    if line is None:
        pytest.skip("no labelled line is a PWM pad: " + ", ".join(p["pad"] for p in lines))
    pad, pin = line["pad"], line["pin"]
    duty = lambda v, **k: [becomes(pad, freq_hz=1000, duty=v / 255, **WAVE, **k)]   # noqa: E731
    conditions = [
        ("rest", None, None, [level(pad, 0)], 0.03),
        ("value=64", f"pwm {pin} 64 1000", rb"PWM (\d)", duty(64), 0.03),               # a start: 1 cut period
        ("value=128", f"pwm {pin} 128 1000", rb"PWM (\d)", duty(128, irregular_max=0), 0.03),
        ("value=192", f"pwm {pin} 192 1000", rb"PWM (\d)", duty(192, irregular_max=0), 0.03),
        ("value=192 again", f"pwm {pin} 192 1000", rb"PWM (\d)", duty(192, irregular_max=0), 0.03),
        ("value=255", f"pwm {pin} 255 1000", rb"PWM (\d)", [becomes(pad, level=1)], 0.03),   # a plain level
        ("value=0", f"pwm {pin} 0 1000", rb"PWM (\d)", [becomes(pad, level=0)], 0.03),
        ("value=128 at 1 kHz", f"pwm {pin} 128 1000", rb"PWM (\d)", duty(128), 0.03),
        ("value=128 at 2 kHz", f"pwm {pin} 128 2000", rb"PWM (\d)",
         [becomes(pad, freq_hz=2000, duty=0.5, irregular_max=2, **WAVE)], 0.03),         # a new frequency: 2
        ("value=128 at 500 Hz", f"pwm {pin} 128 500", rb"PWM (\d)",
         [becomes(pad, freq_hz=500, duty=0.5, irregular_max=2, **WAVE)], 0.06),
        ("stopped", f"pwmoff {pin}", rb"PWMOFF", [becomes(pad, level=0)], 0.03),
    ]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"pwm {pad}", conditions)
        assert all(m.group(1) == b"1" for m in replies if m.group(0).startswith(b"PWM ")), \
            f"{pad}: analogWrite() found no PWM there"
    finally:
        ask(dut, f"pwmoff {pin}", rb"PWMOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_servo(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad, pin = line["pad"], line["pin"]
    width = lambda us, **k: [becomes(pad, period_s=0.02, high_s=us * 1e-6, high_abs_s=30e-6, **WAVE, **k)]   # noqa: E731
    conditions = [("rest", None, None, [level(pad, 0)], 0.05),
                  ("us=544", f"servo {pin} 544", rb"SERVO (\d)", width(544), 0.5)]                    # a start
    conditions += [(f"us={us}", f"servo {pin} {us}", rb"SERVO (\d)", width(us, irregular_max=0), 0.5)
                   for us in (1000, 1500, 2000, 2400)]
    conditions += [("us=2400 again", f"servo {pin} 2400", rb"SERVO (\d)", width(2400, irregular_max=0), 0.5),
                   ("detach", "servooff", rb"SERVOOFF", [becomes(pad, level=0)], 0.06)]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"servo {pad}", conditions)
        assert all(m.group(1) == b"1" for m in replies if m.group(0).startswith(b"SERVO ")), "attach() failed"
    finally:
        ask(dut, "servooff", rb"SERVOOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_tone(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad, pin = line["pad"], line["pin"]
    note = lambda hz: [becomes(pad, freq_hz=hz, duty=0.5, duty_abs=0.01, **WAVE)]   # 1 cut period
    conditions = [("rest", None, None, [level(pad, 0)], 0.03),
                  ("hz=440", f"tone {pin} 440", rb"TONE", note(440), 0.06),
                  ("hz=2000", f"tone {pin} 2000", rb"TONE", note(2000), 0.03),
                  ("hz=2000 again", f"tone {pin} 2000", rb"TONE", note(2000), 0.03),
                  ("notone", f"notone {pin}", rb"NOTONE", [becomes(pad, level=0)], 0.03),
                  ("1 kHz for 30 ms", f"toned {pin} 1000 30", rb"TONE",
                   [pulses(pad, count=30, period_s=1e-3, period_rel=0.02), ends({pad: 0})], 0.08)]
    try:
        sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"tone {pad}", conditions)
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


def rgb_conditions(pad, pin, chains=(3,)):
    """test_rgb_led's conditions: one LED, the same again, and each of `chains` LEDs in one call."""
    one = "a5123c"                                  # green, red, blue of LED 0
    conditions = [("one LED", f"rgb {pin} 1", rb"RGB (\d+)", [rgb_led(pad, data=[one])], 0.02),
                  ("one LED again", f"rgb {pin} 1", rb"RGB (\d+)", [rgb_led(pad, data=[one])], 0.02)]
    # A chain is one ch32rv_rgb_led_write(): one frame, every bit judged as the single LEDs'.
    # A chain of separate rgbLedWrite() calls leaves the line low for the call between LEDs (13 us
    # on V006 at 24 MHz, 2026-11-17): not a promise of the core, not judged here.
    for n in chains:
        chain = "".join(f"a5{0x12 + i:02x}{0x3c ^ i:02x}" for i in range(n))
        conditions.append((f"{n} LEDs in one call", f"rgbbuf {pin} {n}", rb"RGB (\d+)",
                           [rgb_led(pad, data=[chain])], 0.02))
    return conditions


def rgb_hardware(dut, pin):
    return ask(dut, f"rgbhw {pin}", rb"RGBHW (\d)").group(1) == b"1"


def test_rgb_led(dut, oep_host, ws_run, record_property):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    pad, pin = line["pad"], line["pin"]
    # The CPU's frame on most pads; on a pad of the WS2812 block (V4x7, X3x5) the block's, judged the same.
    record_property("hardware", int(rgb_hardware(dut, pin)))
    try:
        sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"rgb {pad}", rgb_conditions(pad, pin),
              rate=60_000_000)
    finally:
        ask(dut, f"input {pin}", rb"INPUT")


def test_rgb_led_hardware(dut, oep_host, ws_run):
    """test_rgb_led on a pad of the WS2812 block (ARGB: the DUT says which), its frames by the block and its DMA:
    the same windows, and eight LEDs in one call besides three. The line is a plain `<port>@<pad>` one or the
    `<port>.argb@<pad>` role (an LED on it as well: the probe only reads it here)."""
    case = settings(__file__)
    candidates = gpio_lines(oep_host, case["target"])
    found, _ = bench_lines.lines(oep_host, case["target"], "argb")
    if "argb" in found:
        candidates = [found["argb"]] + [c for c in candidates if c["pad"] != found["argb"]["pad"]]
    if not candidates:
        pytest.skip("the probe labels no <port>@<pad> or <port>.argb@<pad> line")
    ready(dut, case)
    line = next((c for c in candidates if rgb_hardware(dut, c["pin"])), None)
    if line is None:
        pytest.skip("no labelled line is a pad of the WS2812 block (ch32rv_rgb_led_is_hardware()): "
                    + ", ".join(c["pad"] for c in candidates))
    pad, pin = line["pad"], line["pin"]
    lines = [line] + [p for p in gpio_lines(oep_host, case["target"]) if p["pad"] != pad][:2]
    try:
        sweep(dut, ws_run, oep_host, lines, f"rgb hardware {pad}", rgb_conditions(pad, pin, chains=(3, 8)),
              rate=60_000_000)
    finally:
        ask(dut, f"input {pin}", rb"INPUT")


# ---- the timer features (2026-11-17) ----

HAS_TIMER_OUTPUT, HAS_EDGE_CAPTURE, HAS_TICKER = 1, 2, 4
ROLE_CH, ROLE_CHN, ROLE_BKIN = 0, 1, 2


def has(dut, bit, what):
    """Skip unless the DUT's build holds `bit` of `has` (V003's 16 KB leaves some out)."""
    if not int(ask(dut, "has", rb"HAS (\d+)").group(1)) & bit:
        pytest.skip(f"{what} is not in this build of the signals sketch (V003's 16 KB)")


def pads(dut, pin, pwm=False):
    """The pad's ways to a timer as the DUT's table lists them (the order analogWrite() tries them):
    [(timer, channel, role, route)]. pwm: only the timers analogWrite() may take (not tone()'s / Servo's TIM4 /
    TIM5, CH32RV_PWM_TIMER_MASK)."""
    out = []
    for n in range(24):
        timer, channel, role, route, may = (int(g) for g in ask(
            dut, f"pad {pin} {n}", rb"PAD (\d+) (\d+) (\d+) (\d+) (\d)").groups())
        if not timer:
            break
        if may or not pwm:
            out.append((timer, channel, role, route))
    return out


def default_pwm(dut, pin):
    m = ask(dut, f"caps {pin}", rb"CAPS \d+ (\d+) (\d+)")
    return int(m.group(1)), int(m.group(2))


def duty_sweep(dut, ws_run, oep_host, case, line, title):
    """analogWrite() of 64 and 192 at 1 kHz on the line, then stopped (test_pwm's conditions, fewer)."""
    pad, pin = line["pad"], line["pin"]
    duty = lambda v, **k: [becomes(pad, freq_hz=1000, duty=v / 255, **WAVE, **k)]   # noqa: E731
    conditions = [
        ("rest", None, None, [level(pad, 0)], 0.03),
        ("value=64", f"pwm {pin} 64 1000", rb"PWM (\d)", duty(64), 0.03),
        ("value=192", f"pwm {pin} 192 1000", rb"PWM (\d)", duty(192, irregular_max=0), 0.03),
        ("stopped", f"pwmoff {pin}", rb"PWMOFF", [becomes(pad, level=0)], 0.03),
    ]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), title, conditions)
        assert all(m.group(1) == b"1" for m in replies if m.group(0).startswith(b"PWM ")), \
            f"{pad}: analogWrite() found no PWM there"
    finally:
        ask(dut, f"pwmoff {pin}", rb"PWMOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_pwm_route(dut, oep_host, ws_run, record_property):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    for line in lines:
        ways = [w for w in pads(dut, line["pin"], pwm=True) if w[2] == ROLE_CH]
        if ways and default_pwm(dut, line["pin"]) == (0, 0):
            break
    else:
        pytest.skip("no labelled line is a timer channel's pad on another route only: " +
                    ", ".join(p["pad"] for p in lines))
    timer, channel, _, route = ways[0]
    record_property("way", f"{line['pad']} TIM{timer} CH{channel} route {route}")
    duty_sweep(dut, ws_run, oep_host, case, line, f"pwm on another route {line['pad']}")
    ask(dut, f"pwm {line['pin']} 128 1000", rb"PWM (\d)")
    record_property("route_set", int(ask(dut, f"route {timer}", rb"ROUTE (\d+)").group(1)))
    ask(dut, f"pwmoff {line['pin']}", rb"PWMOFF")


def test_pwm_complementary(dut, oep_host, ws_run, record_property):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    for line in lines:
        ways = [w for w in pads(dut, line["pin"], pwm=True) if w[2] in (ROLE_CH, ROLE_CHN)]
        if ways and ways[0][2] == ROLE_CHN:          # analogWrite() takes the CHxN
            break
    else:
        pytest.skip("no labelled line is a complementary output (CHxN) that analogWrite() takes first: " +
                    ", ".join(p["pad"] for p in lines))
    record_property("way", f"{line['pad']} " + " ".join(f"TIM{t} CH{c}{'N' if r else ''} route {x}"
                                                        for t, c, r, x in ways))
    duty_sweep(dut, ws_run, oep_host, case, line, f"pwm on CHxN {line['pad']}")


def complementary_pair(dut, lines):
    """(A, B, timer, channel, route, ways): A a channel's pad and B its complementary one on one timer, channel and
    route, or None."""
    ways = {line["pad"]: pads(dut, line["pin"]) for line in lines}
    for a in lines:
        for t, c, role, r in ways[a["pad"]]:
            if role != ROLE_CH:
                continue
            for b in lines:
                if b is not a and (t, c, ROLE_CHN, r) in ways[b["pad"]]:
                    return a, b, t, c, r, ways
    return None


def test_complementary(dut, oep_host, ws_run, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    has(dut, HAS_TIMER_OUTPUT, "CH32RVComplementaryPwm")
    found = complementary_pair(dut, lines)
    if found is None:
        pytest.skip("no two labelled lines are a CHx and its CHxN on one route: " + ", ".join(p["pad"] for p in lines))
    a, b, timer, channel, route, ways = found
    brk = next((line for line in lines if line not in (a, b) and (timer, 0, ROLE_BKIN, route) in ways[line["pad"]]),
               None)
    hi, lo, pa, pb = a["pad"], b["pad"], a["pin"], b["pin"]
    record_property("pair", f"{hi} TIM{timer} CH{channel}, {lo} CH{channel}N, route {route}, "
                            f"break {brk['pad'] if brk else 'none'}")
    both = [becomes(hi, freq_hz=20000, **WAVE), becomes(lo, freq_hz=20000, **WAVE)]
    off = [becomes(hi, level=0), becomes(lo, level=0)]
    conditions = [("rest", None, None, [level(hi, 0), level(lo, 0)], 0.01)]
    conditions += [(f"dead {ns} ns asked", f"comp {pa} {pb} 20000 128 {ns}", rb"COMP (\d) (\d+)", both, 0.01)
                   for ns in (500, 1000)]
    conditions.append(("stopped", "compoff", rb"COMPOFF", off, 0.01))
    try:
        replies = sweep(dut, ws_run, oep_host, [a, b], f"complementary {hi} {lo}", conditions, rate=10_000_000,
                        moving=2, quiet=[])
        comp = [m for m in replies if m.group(0).startswith(b"COMP ")]
        assert all(m.group(1) == b"1" for m in comp), "begin() refused the pair"
        sets = [int(m.group(2)) for m in comp]
        record_property("dead_set_ns", sets)
        # The dead time judged on its own section, against what the DUT says each request set (the field's steps);
        # the start (2 ms) left out: before it both lines rest low.
        for ns, got in zip((500, 1000), sets):
            conditions = [("rest", None, None, [level(hi, 0), level(lo, 0)], 0.005),
                          (f"dead {got} ns", f"comp {pa} {pb} 20000 128 {ns}", rb"COMP (\d) (\d+)",
                           [trim(dead_time(hi, lo, dead_min_s=(got - 100) * 1e-9, dead_max_s=(got + 100) * 1e-9),
                                 head_s=0.002)], 0.01),
                          ("stopped", "compoff", rb"COMPOFF", off, 0.005)]
            sweep(dut, ws_run, oep_host, [a, b], f"dead time {got} ns {hi} {lo}", conditions, rate=100_000_000,
                  moving=2, quiet=[])
    finally:
        ask(dut, "compoff", rb"COMPOFF")
    if brk is None:
        record_property("break", "no labelled line is the timer's break input on this route")
        return
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, brk["channel"])])
    try:
        gpio.configure(brk["channel"], Gpio.OUTPUT_HIGH)
        running = [becomes(hi, freq_hz=20000, **WAVE)]
        conditions = [
            ("running", f"comp {pa} {pb} 20000 128 500", rb"COMP (\d) (\d+)", running, 0.01),
            ("break attached", f"brk {brk['pin']} 0", rb"BRK (\d)", [], 0.005),
            ("break", lambda: gpio.configure(brk["channel"], Gpio.OUTPUT_LOW), None,
             [becomes(hi, level=0, within_s=0.05), becomes(lo, level=0, within_s=0.05)], 0.01),
            ("released", lambda: gpio.configure(brk["channel"], Gpio.OUTPUT_HIGH), None,
             [level(hi, 0), level(lo, 0)], 0.005),
            ("latched", f"brkstate {brk['pin']}", rb"BRKSTATE (\d) (\d)", [level(hi, 0), level(lo, 0)], 0.005),
            ("resume", "resume", rb"RESUME (\d)", running, 0.01),
            ("stopped", "compoff", rb"COMPOFF", off, 0.005),
        ]
        replies = sweep(dut, ws_run, oep_host, [a, b], f"break {brk['pad']}", conditions, rate=10_000_000,
                        moving=2, quiet=[])
        said = {m.group(0).split()[0].decode(): m.group(1) for m in replies if m.re.groups}
        state = next((m for m in replies if m.group(0).startswith(b"BRKSTATE")), None)
        if state is not None:
            record_property("break_pad_level_released", int(state.group(2)))
        assert said.get("BRK") == b"1", f"{brk['pad']}: attachBreak() refused it"
        assert said.get("BRKSTATE") == b"1", "the break was not latched"
        assert said.get("RESUME") == b"1", "resume() did not turn the outputs back on"
    finally:
        ask(dut, "compoff", rb"COMPOFF")
        gpio.configure(brk["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])


def channel_line(dut, lines, prefer=None):
    """(line, its channel ways): the first labelled line that is a timer channel's pad, preferring one for which
    prefer(line, ways) is true; (None, None) when there is none."""
    plain = None
    for line in lines:
        ways = [w for w in pads(dut, line["pin"]) if w[2] == ROLE_CH]
        if not ways:
            continue
        if prefer is None or prefer(line, ways):
            return line, ways
        plain = plain or (line, ways)
    return plain or (None, None)


def test_one_pulse(dut, oep_host, ws_run):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    has(dut, HAS_TIMER_OUTPUT, "CH32RVOnePulse")
    line, _ = channel_line(dut, lines)
    if line is None:
        pytest.skip("no labelled line is a timer channel's pad: " + ", ".join(p["pad"] for p in lines))
    pad, pin = line["pad"], line["pin"]
    one = lambda us: [pulses(pad, count=1, width_min_s=us * 0.98e-6 - 0.4e-6,   # noqa: E731
                             width_max_s=us * 1.02e-6 + 0.4e-6)]
    conditions = [("rest", None, None, [level(pad, 0)], 0.01),
                  ("50 us after 200 us", f"shot {pin} 200 50", rb"SHOT (\d)", one(50), 0.01),
                  ("1 ms after 100 us", f"shot {pin} 100 1000", rb"SHOT (\d)", one(1000), 0.01),
                  ("end", "shotoff", rb"SHOTOFF", [level(pad, 0)], 0.005)]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"one pulse {pad}", conditions,
                        rate=5_000_000)
        assert all(m.group(1) == b"1" for m in replies if m.group(0).startswith(b"SHOT ")), "fire() refused"
    finally:
        ask(dut, "shotoff", rb"SHOTOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_output_compare(dut, oep_host, ws_run):
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    has(dut, HAS_TIMER_OUTPUT, "CH32RVOutputCompare")
    line, _ = channel_line(dut, lines)
    if line is None:
        pytest.skip("no labelled line is a timer channel's pad: " + ", ".join(p["pad"] for p in lines))
    pad, pin = line["pad"], line["pin"]
    square = lambda **k: [becomes(pad, freq_hz=500, duty=0.5, **WAVE, **k)]   # noqa: E731
    conditions = [("rest", None, None, [level(pad, 0)], 0.01),
                  ("toggle at 250 us", f"oc {pin} 1000 250 3", rb"OC (\d)", square(), 0.05),
                  ("toggle at 600 us", f"oc {pin} 1000 600 3", rb"OC (\d)", square(), 0.05),
                  ("force active", f"oc {pin} 1000 600 5", rb"OC (\d)", [becomes(pad, level=1)], 0.01),
                  ("force inactive", f"oc {pin} 1000 600 4", rb"OC (\d)", [becomes(pad, level=0)], 0.01),
                  ("end", "ocoff", rb"OCOFF", [level(pad, 0)], 0.005)]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"output compare {pad}",
                        conditions)
        assert all(m.group(1) == b"1" for m in replies if m.group(0).startswith(b"OC ")), "begin() / set() refused"
    finally:
        ask(dut, "ocoff", rb"OCOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def probe_pwm(oep_host, line):
    from oep_client import core
    from oep_client.fixture import Pwm
    host = oep_host.host
    pwm = Pwm(host)
    core.plan_apply(host, pwm.assignments(line["channel"]))
    return pwm


def release_pwm(oep_host, pwm):
    from oep_client import core
    pwm.stop(0)
    core.plan_release(oep_host.host, [pwm.fn])


def test_edge_capture(dut, oep_host, record_property):
    from oep_client.fixture import Pwm
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    has(dut, HAS_EDGE_CAPTURE, "CH32RVInputCapture")
    # CH3 / CH4, or a pad off the default route, first: what CH32RVPulseCapture cannot reach.
    line, ways = channel_line(dut, lines, prefer=lambda line, ways: ways[0][1] in (3, 4)
                              or default_pwm(dut, line["pin"]) == (0, 0))
    if line is None:
        pytest.skip("no labelled line is a timer channel's pad: " + ", ".join(p["pad"] for p in lines))
    record_property("line", f"{line['pad']} " + " ".join(f"TIM{t} CH{c} route {r}" for t, c, _, r in ways))
    pin = line["pin"]
    pwm = probe_pwm(oep_host, line)
    try:
        made = pwm.start(0, 1000, round(0.25 * Pwm.FULL))
        period = 1e6 / made.hz
        high = period * made.duty / Pwm.FULL
        record_property("made", f"{made.hz:.3f} Hz {made.duty / Pwm.FULL:.4f}")
        for edge, name in ((3, "change"), (1, "rising"), (2, "falling")):
            assert ask(dut, f"icap {pin} {edge}", rb"ICAP (\d)").group(1) == b"1", f"begin() refused {line['pad']}"
            time.sleep(0.05)
            m = ask(dut, "icread", rb"EDGES (\d+) (\d+) (\d+) (-?\d+)((?: \d+)*)")
            ask(dut, "icoff", rb"ICOFF")
            n, tick, rising = int(m.group(1)), int(m.group(3)), int(m.group(4))
            gaps = [int(g) * 1e6 / tick for g in m.group(5).split()]
            record_property(f"{name}_gaps_us", gaps)
            assert n == 8, f"{name}: {n} edges queued in 50 ms of a 1 kHz signal"
            if edge == 3:
                # From a rising edge the high time comes next, from a falling one the low time.
                want = [high if (k % 2 == 0) == bool(rising) else period - high for k in range(len(gaps))]
            else:
                assert rising == (edge == 1), f"{name}: the first edge is {'rising' if rising else 'falling'}"
                want = [period] * len(gaps)
            for k, (got, w) in enumerate(zip(gaps, want)):
                assert abs(got - w) <= w * 0.01 + 1e6 / tick, f"{name}: gap {k} {got:.1f} us for {w:.1f}"
    finally:
        release_pwm(oep_host, pwm)
        ask(dut, "icoff", rb"ICOFF")


def test_pulse_counter(dut, oep_host, record_property):
    """CH32RVPulseCounter: the probe's PWM (20 kHz) on a channel 1 or 2 pad clocks the timer itself (external clock
    mode 1). Over 200 ms of the DUT's micros(), RISING and FALLING count the probe's frequency and CHANGE twice it,
    within 2 % (the DUT's clock may be HSI) and a count."""
    from oep_client.fixture import Pwm
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    has(dut, HAS_EDGE_CAPTURE, "CH32RVPulseCounter")
    line, ways = channel_line(dut, lines, prefer=lambda line, ways: ways[0][1] == 1)
    if line is None or not any(c in (1, 2) for _, c, _, _ in ways):
        pytest.skip("no labelled line is a channel 1 or 2 pad of a timer")
    record_property("line", f"{line['pad']} " + " ".join(f"TIM{t} CH{c} route {r}" for t, c, _, r in ways))
    pin = line["pin"]
    channel_one = any(c == 1 for _, c, _, _ in ways)
    pwm = probe_pwm(oep_host, line)
    try:
        made = pwm.start(0, 20000, round(0.5 * Pwm.FULL))
        record_property("made_hz", round(made.hz, 3))
        for edge, name, per in ((1, "rising", 1), (2, "falling", 1), (3, "change", 2)):
            if edge == 3 and not channel_one:
                continue
            m = ask(dut, f"pcount {pin} {edge} 200", rb"PCOUNT (\d) (\d+) (\d+)", timeout=5)
            assert m.group(1) == b"1", f"{name}: begin() refused {line['pad']}"
            n, us = int(m.group(2)), int(m.group(3))
            want = made.hz * per * us / 1e6
            record_property(f"{name}_count", n)
            assert abs(n - want) <= want * 0.02 + 1, f"{name}: {n} counts in {us} us, want {want:.0f}"
    finally:
        release_pwm(oep_host, pwm)


def dut_stamp(dut):
    """(host time at the reply's midpoint, DUT micros(), the round trip in s)."""
    sent = time.monotonic()
    us = int(ask(dut, "now", rb"NOW (\d+)").group(1))
    back = time.monotonic()
    return (sent + back) / 2, us, back - sent


def test_timer_pulse_in(dut, oep_host, record_property):
    """timerPulseIn() (the opt-in capture; pulseIn() itself stays the software loop) against the probe's PWM.

    The DUT counts on its own clock (an internal oscillator: X035 ran 2.8 % fast, 2026-11-17), so the widths are
    judged against the DUT's timebase, measured here: its micros() against the host's clock over the test (at least
    2 s), each reading taken at the midpoint of its console round trip. What a width may be off by: the rate's
    uncertainty (half the two round trips over the span) times the width, plus 2 ticks (each edge latched to 1 us),
    plus 1 us for the pad's edges against the probe's."""
    from oep_client.fixture import Pwm
    case = settings(__file__)
    lines = labelled_lines(oep_host, case)
    ready(dut, case)
    line, ways = channel_line(dut, lines)
    if line is None:
        pytest.skip("no labelled line is a timer channel's pad: " + ", ".join(p["pad"] for p in lines))
    record_property("line", f"{line['pad']} " + " ".join(f"TIM{t} CH{c} route {r}" for t, c, _, r in ways))
    pwm = probe_pwm(oep_host, line)
    got = []
    try:
        h0, us0, rtt0 = dut_stamp(dut)
        for hz, duty in ((1000, 0.25), (10000, 0.5)):
            made = pwm.start(0, hz, round(duty * Pwm.FULL))
            high = 1e6 / made.hz * made.duty / Pwm.FULL
            for state, want in ((1, high), (0, 1e6 / made.hz - high)):
                width = int(ask(dut, f"tpulsein {line['pin']} {state} 100000", rb"TPULSEIN (\d+)").group(1))
                got.append((hz, state, want, width, made.hz))
        time.sleep(max(0.0, 2.0 - (time.monotonic() - h0)))
        h1, us1, rtt1 = dut_stamp(dut)
    finally:
        release_pwm(oep_host, pwm)
    span = h1 - h0
    rate = ((us1 - us0) & 0xFFFFFFFF) / 1e6 / span           # DUT seconds per host second
    rate_unc = (rtt0 + rtt1) / 2 / span
    record_property("dut_rate", f"{rate:.5f} +- {rate_unc:.5f} over {span:.2f} s")
    for hz, state, want, width, made_hz in got:
        expect = want * rate
        allowed = expect * rate_unc + 3.0
        record_property(f"{hz}_{state}", f"{width} us for {expect:.1f} (+- {allowed:.1f}; {want:.1f} on the probe's clock)")
        assert abs(width - expect) <= allowed, \
            f"timerPulseIn({state}) on {line['pad']}: {width} us for {expect:.1f} +- {allowed:.1f} " \
            f"({want:.1f} us at {made_hz:.1f} Hz on the probe's clock, the DUT's clock at {rate:.5f})"


def test_ticker(dut, oep_host, ws_run):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    has(dut, HAS_TICKER, "Ticker")
    pad, pin = line["pad"], line["pin"]
    toggle = lambda hz, **k: [becomes(pad, freq_hz=hz, duty=0.5, duty_abs=0.02, **WAVE, **k)]   # noqa: E731
    once = lambda us: [pulses(pad, count=1, width_min_s=us * 0.98e-6 - 20e-6,   # noqa: E731
                              width_max_s=us * 1.02e-6 + 20e-6)]
    conditions = [("rest", f"level {pin} 0", rb"LEVEL", [], 0.01),
                  ("attach_us 500", f"tick {pin} 500 0", rb"TICK (\d)", toggle(1000), 0.04),
                  ("attach_us 100", f"tick {pin} 100 0", rb"TICK (\d)", toggle(5000, irregular_max=2), 0.02),
                  ("detach", "tickoff", rb"TICKOFF", [], 0.005),
                  ("low", f"level {pin} 0", rb"LEVEL", [ends({pad: 0})], 0.005),
                  ("once_us 2000", f"tick {pin} 2000 1", rb"TICK (\d)", once(2000), 0.02)]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"ticker {pad}", conditions)
        assert all(m.group(1) == b"1" for m in replies if m.group(0).startswith(b"TICK ")), "no timer for the Ticker"
    finally:
        ask(dut, "tickoff", rb"TICKOFF")
        ask(dut, f"input {pin}", rb"INPUT")


def test_hw_timer(dut, oep_host, ws_run, record_property):
    case = settings(__file__)
    line = first_line(oep_host, case)
    ready(dut, case)
    has(dut, HAS_TICKER, "the timer API")
    pad, pin = line["pad"], line["pin"]
    conditions = [("rest", f"level {pin} 0", rb"LEVEL", [], 0.01),
                  ("alarm every 250 us", f"htimer {pin} 250 0", rb"HTIMER (\d) (\d+)",
                   [becomes(pad, freq_hz=2000, duty=0.5, duty_abs=0.02, **WAVE)], 0.03),
                  ("end", "htimeroff", rb"HTIMEROFF", [], 0.005),
                  ("low", f"level {pin} 0", rb"LEVEL", [ends({pad: 0})], 0.005),
                  ("one alarm at 1500 us", f"htimer {pin} 1500 1", rb"HTIMER (\d) (\d+)",
                   [pulses(pad, count=1, width_min_s=1500 * 0.98e-6 - 20e-6, width_max_s=1500 * 1.02e-6 + 20e-6)],
                   0.02)]
    try:
        replies = sweep(dut, ws_run, oep_host, with_neighbours(oep_host, case, line), f"timer API {pad}", conditions)
        said = [m for m in replies if m.group(0).startswith(b"HTIMER ")]
        assert all(m.group(1) == b"1" for m in said), "timerBegin() found no free timer"
        record_property("frequency", [int(m.group(2)) for m in said])
        assert all(int(m.group(2)) == 1000000 for m in said), "timerBegin(1000000) counts at another rate"
    finally:
        ask(dut, "htimeroff", rb"HTIMEROFF")
        ask(dut, f"input {pin}", rb"INPUT")
