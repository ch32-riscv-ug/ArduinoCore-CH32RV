"""periph (case periph): the single-line peripherals and the planned ones, on the lines the probe labels
(bench_lines.py, tests/BENCH.ja.md §5). Each test skips when its labels are not there.

- periph.uart2: Serial1 on the uart2_rx / uart2_tx pads echoes 200 random bytes the probe's UART sends, 115200 8N1.
- periph.pwm: analogWrite() of 64 / 255 at 1 kHz on the pwm pad: the probe's capture sees 1 kHz within 2 % and a
  duty of 25 % within 2 points.
- periph.adc: the probe drives the adc pad low, then high: analogRead() is under 5 % of full scale, then over 95 %.
- periph.can / i2s / argb / rs485 / qspi: not implemented in the core. With their lines labelled the test fails,
  saying so; that is the reminder that the bench is ready for them."""
import json
import pathlib
import secrets
import sys
import time

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py is staged beside the test
from bench_lines import require


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def ready(dut, case):
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"


def test_uart2(dut, oep_host, record_property):
    from oep_client import core, fixture, host as oh, registry
    case = settings(__file__)
    lines = require(oep_host, case["target"], "uart2")
    ready(dut, case)
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    # The probe receives what the DUT sends (uart2_tx) and sends to what it receives (uart2_rx).
    probe_rx, probe_tx = lines["uart2_tx"]["channel"], lines["uart2_rx"]["channel"]
    io, used = None, None
    for fn in core.find_all(host, "oep.fixture.uart"):
        try:
            core.plan_apply(host, [(fn, RX, probe_rx), (fn, TX, probe_tx)])
        except oh.Rejected:
            continue          # the console's, or busy
        io, used = fixture.FixtureUartIO(host, fn), fn
        break
    assert io is not None, "no free oep.fixture.uart on the probe for the second UART"
    try:
        io.configure(115200)
        m = ask(dut, f"uart2 {lines['uart2_rx']['pin']} {lines['uart2_tx']['pin']} 115200", rb"UART2 (\d)")
        assert m.group(1) == b"1", "Serial1 did not start on those pads"
        payload = bytes([0, 0x55, 0xAA, 0xFF]) + secrets.token_bytes(196)
        io.write(payload)
        got, deadline = b"", time.monotonic() + 5
        while len(got) < len(payload) and time.monotonic() < deadline:
            got += io.read(480)
        assert got[:len(payload)] == payload, f"echo differs ({len(got)} of {len(payload)} bytes back)"
        m = ask(dut, "stop", rb"STOP (\d+)")
        record_property("echoed", int(m.group(1)))
    finally:
        core.plan_release(host, [used])


def test_pwm(dut, oep_host, record_property):
    from oep_client import capture, core
    case = settings(__file__)
    lines = require(oep_host, case["target"], "pwm")
    ready(dut, case)
    host = oep_host.host
    line = lines["pwm"]
    m = ask(dut, f"pwm {line['pin']} 64 1000", rb"PWM (\d)")
    assert m.group(1) == b"1", f"{line['pad']} has no PWM"
    cap = capture.LogicCapture(host)
    core.plan_apply(host, [(cap.fn, 0, line["channel"])])
    try:
        cap.configure(rate=1_000_000, samples=20_000)
        cap.start()
        segs = cap.wait(timeout=5.0)
        bits = cap.channel(cap.read_segment(segs[0]), 0, segs[0].samples)
    finally:
        core.plan_release(host, [cap.fn])
        ask(dut, f"pwmoff {line['pin']}", rb"PWMOFF")
    rises = [i for i in range(1, len(bits)) if bits[i] and not bits[i - 1]]
    assert len(rises) >= 5, f"{len(rises)} rising edges in 20 ms"
    span = rises[-1] - rises[0]
    period_us = span / (len(rises) - 1)
    high = sum(bits[rises[0]:rises[-1]])
    duty = high / span
    record_property("period_us", round(period_us, 1))
    record_property("duty", round(duty, 4))
    assert abs(period_us - 1000) <= 20, f"period {period_us:.1f} us"
    assert abs(duty - 64 / 255) <= 0.02, f"duty {duty:.3f}"


def test_adc(dut, oep_host, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    case = settings(__file__)
    lines = require(oep_host, case["target"], "adc")
    ready(dut, case)
    host = oep_host.host
    line = lines["adc"]
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, line["channel"])])
    try:
        readings = {}
        for level, mode in ((0, Gpio.OUTPUT_LOW), (1, Gpio.OUTPUT_HIGH)):
            gpio.configure(line["channel"], mode)
            time.sleep(0.01)
            m = ask(dut, f"adc {line['pin']}", rb"ADC (\d+) (\d+)")
            readings[level] = (int(m.group(1)), int(m.group(2)))
        record_property("readings", json.dumps(readings))
        low, full = readings[0]
        high, _ = readings[1]
        assert low <= full * 0.05, f"{line['pad']} driven low reads {low} of {full}"
        assert high >= full * 0.95, f"{line['pad']} driven high reads {high} of {full}"
    finally:
        gpio.configure(line["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])


@pytest.mark.parametrize("feature", ["can", "i2s", "argb", "rs485", "qspi"])
def test_planned(dut, oep_host, feature):
    """Planned features: the names are fixed, the code is not written (FEATURE_CANDIDATES.ja.md)."""
    case = settings(__file__)
    lines = require(oep_host, case["target"], feature)
    pytest.fail(f"{feature}: the bench labels {', '.join(sorted(lines))}, but the core has no {feature} driver yet")
