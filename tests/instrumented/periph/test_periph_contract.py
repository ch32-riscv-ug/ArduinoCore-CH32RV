"""periph (case periph): a second UART and an ADC input on `<port>@<pad>` lines the probe labels (bench_lines.py
gpio_lines), and the planned features on their role labels (tests/BENCH.ja.md §5).

- periph.serial-routes: Serial1 begun on each pair of pads that is a USART route (the variant's routes, place.py
  serial_routes; not the console's USART) with both pads labelled, one after another in one capture of all their
  lines (ws_capture.stream), a WireSkein section per pair: at 115200 8N1 the probe's UART sends 64 bytes (random
  after 00 55 AA FF) and the sketch echoes them; the bytes decoded on the DUT's RX and TX (uart: the format, F_CPU /
  BRR within 3 %, no framing error), only that pair moving (a remap that lands elsewhere moves another line), every
  line idle at its start and end (the probe's input pull-ups hold the lines the section does not use). Serial1 must take a USART the pair is a route of (peripheral()), and what came
  back to the probe is compared here.
- periph.adc: a labelled pad the DUT names as an ADC channel (not one a defect of the board's record takes): the probe
  drives it low, then high: analogRead() is under a quarter of full scale, then over three quarters (whether it works;
  the ADC's accuracy is a manual test with an external source).
- periph.adc-stream: CH32RVAdcStream on one or two such pads, the probe's PWM on the first (test_adc_stream).
- periph.hsadc: CH32RVHsAdc on an HSADC pad on VDDIO (PF8-PF10), the probe's PWM on it (test_hsadc).
- periph.can / i2s / argb / rs485 / qspi: not implemented in the core. With their lines labelled the test fails,
  saying so; that is the reminder that the bench is ready for them."""
import json
import pathlib
import secrets
import sys
import time

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_lines.py, ws_capture.py are staged beside the test
from bench_lines import gpio_lines, port_lines, require
import ws_capture


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def ready(dut, case):
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"


def test_serial_routes(dut, oep_host, ws_run, record_property):
    from oep_client import core, fixture, host as oh, registry
    from oep_client.fixture import Gpio
    from wireskein.runlog import ends, only_moving, starts, uart
    case = settings(__file__)
    target = case["target"]
    lines = {line["pad"]: line for line in gpio_lines(oep_host, target)}
    console = {line["pad"] for line in port_lines(oep_host, target) if line["role"] in ("rx", "tx")}
    routes = target.get("serial_routes", [])
    console_usarts = {n for n, _, tx, rx in routes if {tx, rx} & console}
    pairs = {}                      # (tx, rx): [(usart, route), ...] - one section per pad pair
    for n, k, tx, rx in routes:
        if n not in console_usarts and tx in lines and rx in lines:
            pairs.setdefault((tx, rx), []).append((n, k))
    if not pairs:
        pytest.skip("no USART route besides the console's has both pads labelled <port>@<pad>")
    record_property("routes", json.dumps({f"{tx}/{rx}": [f"USART{n} route {k}" for n, k in nk]
                                          for (tx, rx), nk in pairs.items()}))
    ready(dut, case)
    host = oep_host.host
    RX, TX = (registry.FIXTURE_UART.enum["role"][r] for r in ("rx", "tx"))
    uarts = core.find_all(host, "oep.fixture.uart")
    f_cpu = int(target["f_cpu"])
    pads = sorted({p for pair in pairs for p in pair})
    wrong = []

    gpio = Gpio(host)

    def probe_uart(tx, rx):
        """The probe's UART on this pair (the console's is busy), and its pads' input pull-ups on every other line
        (released or never driven, they would float): (io, fn)."""
        core.plan_release(host, [gpio.fn])
        others = [lines[p]["channel"] for p in pads if p not in (tx, rx)]
        if others:
            core.plan_apply(host, [(gpio.fn, 1, ch) for ch in others])
            for ch in others:
                gpio.configure(ch, Gpio.INPUT_PULLUP)
        for fn in uarts:
            try:
                core.plan_apply(host, [(fn, RX, lines[tx]["channel"]), (fn, TX, lines[rx]["channel"])])
            except oh.Rejected:
                continue
            return fixture.FixtureUartIO(host, fn), fn
        raise AssertionError("no free oep.fixture.uart on the probe for Serial1")

    def steps(wait):
        idle = {p: 1 for p in pads}
        with ws_run.section(1, f"Serial1 routes {' '.join(f'{tx}/{rx}' for tx, rx in pairs)}"):
            for (tx, rx), usarts in pairs.items():
                payload = bytes([0, 0x55, 0xAA, 0xFF]) + secrets.token_bytes(60)
                io, used = probe_uart(tx, rx)
                try:
                    probe_baud = io.configure(115200)
                    name = " / ".join(f"USART{n} route {k}" for n, k in usarts) + f" TX {tx} RX {rx}"
                    with ws_run.section(2, name, expect=[
                            uart(rx, probe_baud, data=payload.hex()),
                            uart(tx, f_cpu / round(f_cpu / 115200), data=payload.hex()),
                            only_moving([tx, rx]), starts(idle), ends(idle)]):
                        command = f"uart2 {lines[rx]['pin']} {lines[tx]['pin']} 115200"
                        ws_run.command(command)
                        dut.write(command + "\n")
                        m = dut.expect(rb"UART2 (\d) (\d+)\r?\n", timeout=5)
                        ws_run.reply(m.group(0).decode())
                        took = int(m.group(2))
                        if m.group(1) != b"1" or took not in {n for n, _ in usarts}:
                            wrong.append(f"{name}: Serial1 began {m.group(1).decode()} on USART{took}")
                        wait(0.02)
                        io.write(payload)
                        got = bytearray()
                        deadline = time.monotonic() + 3
                        while len(got) < len(payload) and time.monotonic() < deadline:
                            wait(0.02)
                            got.extend(io.read(480))
                        m = ask(dut, "stop", rb"STOP (\d+)")
                        ws_run.reply(m.group(0).decode())
                        if bytes(got[:len(payload)]) != payload:
                            wrong.append(f"{name}: echo differs ({len(got)} of {len(payload)} bytes back)")
                        wait(0.02)
                finally:
                    core.plan_release(host, [used])

    try:
        ws_capture.stream(ws_run, oep_host, [lines[p] for p in pads], rate=5_000_000, steps=steps)
    finally:
        for p in pads:
            try:
                gpio.configure(lines[p]["channel"], Gpio.INPUT)
            except Exception:
                pass            # not this fn's channel any more
        core.plan_release(host, [gpio.fn])
    assert not wrong, " | ".join(wrong)


def test_adc(dut, oep_host, record_property):
    from oep_client import core
    from oep_client.fixture import Gpio
    case = settings(__file__)
    lost = {int(f.split(":")[1]) for d in case["target"].get("record", {}).get("defects", [])
            for f in d.get("lose", []) if f.startswith("adc:")}
    ready(dut, case)
    line = None
    for candidate in gpio_lines(oep_host, case["target"]):
        channel = int(ask(dut, f"acaps {candidate['pin']}", rb"ACAPS (-?\d+)").group(1))
        if channel >= 0 and channel not in lost:
            line = candidate
            break
    if line is None:
        pytest.skip("no labelled <port>@<pad> line is an ADC channel" + (f" (the record's defects take {sorted(lost)})" if lost else ""))
    record_property("line", f"{line['pad']} channel {channel}")
    host = oep_host.host
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
        # Whether it works, not how well: the probe's levels are no reference (the ADC's own accuracy is a manual
        # test with an external source). Under a quarter / over three quarters of full scale, as adc-stream.
        assert low < full / 4, f"{line['pad']} driven low reads {low} of {full}"
        assert high > full * 3 / 4, f"{line['pad']} driven high reads {high} of {full}"
    finally:
        gpio.configure(line["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])


def test_adc_stream(dut, oep_host, ws_run, record_property):
    """periph.adc-stream: CH32RVAdcStream (the ADC by DMA) on the labelled pads the DUT names as ADC channels (not
    the record's defects'). The probe's PWM drives the first, its GPIO holds the second (when there is one) high or
    low; the sketch reads 16 halves of 256 rounds (96 on V003's 2 KB of RAM) and reports the square wave it saw on the first (samples over 3/4
    of full scale high, under 1/4 low, the rounds of its rises) and the second's range. One capture of the lines, a
    WireSkein section per condition judging the stimulus (becomes: the probe's wave and level); compared here: the
    wave's frequency from the rises (rounds between the first and last over the rate sampleRate() says) within 3 %
    of the probe's, its duty within 0.03 of the probe's, no overrun, and the second pin's range at its level (all over 3/4 of full scale high, under 1/4 low)."""
    from oep_client import core
    from oep_client.fixture import Gpio, Pwm
    from wireskein.runlog import becomes
    case = settings(__file__)
    lost = {int(f.split(":")[1]) for d in case["target"].get("record", {}).get("defects", [])
            for f in d.get("lose", []) if f.startswith("adc:")}
    ready(dut, case)
    found = []
    for candidate in gpio_lines(oep_host, case["target"]):
        channel = int(ask(dut, f"acaps {candidate['pin']}", rb"ACAPS (-?\d+)").group(1))
        if channel >= 0 and channel not in lost:
            found.append((candidate, channel))
        if len(found) == 2:
            break
    if not found:
        pytest.skip("no labelled <port>@<pad> line is an ADC channel")
    (wave, wave_ch), *rest = found
    record_property("lines", json.dumps([f"{line['pad']} channel {ch}" for line, ch in found]))
    host = oep_host.host
    pwm, gpio = Pwm(host), Gpio(host)
    core.plan_apply(host, pwm.assignments(wave["channel"]))
    if rest:
        core.plan_apply(host, [(gpio.fn, 1, rest[0][0]["channel"])])
    # (wave Hz, duty, the second pin's level or None for one pin)
    conditions = [(1000, 0.5, None)]
    if rest:
        conditions += [(2000, 0.25, 1), (1000, 0.75, 0)]
    results, wrong = {}, []

    def steps(wait):
        with ws_run.section(1, f"adc stream {' '.join(line['pad'] for line, _ in found)}"):
            for hz, duty, second in conditions:
                name = f"{hz} Hz duty {duty}" + ("" if second is None else f", {rest[0][0]['pad']} {second}")
                want = [becomes(wave["pad"], freq_hz=hz, duty=duty, irregular_max=3, min_periods=20, duty_abs=0.01)]
                if second is not None:
                    want.append(becomes(rest[0][0]["pad"], level=second))
                with ws_run.section(2, name, expect=want):
                    made = pwm.start(0, hz, round(duty * Pwm.FULL))
                    if second is not None:
                        gpio.configure(rest[0][0]["channel"], Gpio.OUTPUT_HIGH if second else Gpio.OUTPUT_LOW)
                    wait(0.02)
                    pins = [wave["pin"]] + ([rest[0][0]["pin"]] if second is not None else [])
                    command = f"astream 250000 16 {' '.join(str(p) for p in pins)}"
                    ws_run.command(command)
                    dut.write(command + "\n")
                    end = time.monotonic() + 5
                    while True:
                        try:
                            m = dut.expect(rb"ASTREAM ([\d ]+)\r?\n", timeout=0.05)
                            break
                        except Exception:
                            assert time.monotonic() < end, f"{name}: no ASTREAM"
                            wait(0.05)
                    ws_run.reply(m.group(0).decode())
                    v = [int(x) for x in m.group(1).split()]
                    if v[0] == 0:
                        wrong.append(f"{name}: begin() refused")
                        continue
                    rate, step, overruns, rounds, high, low, rises, first, last, lo1, hi1, full = v
                    got_hz = rate * (rises - 1) / (last - first) if rises > 1 and last > first else 0.0
                    got_duty = high / (high + low) if high + low else 0.0
                    results[name] = dict(rate=rate, step=step, rounds=rounds, hz=round(got_hz, 2),
                                         duty=round(got_duty, 4), overruns=overruns, second=[lo1, hi1],
                                         probe_hz=made.hz, probe_duty=made.duty / Pwm.FULL)
                    if abs(got_hz / made.hz - 1) > 0.03:
                        wrong.append(f"{name}: {got_hz:.1f} Hz from the stream, the probe makes {made.hz:.1f}")
                    if abs(got_duty - made.duty / Pwm.FULL) > 0.03:
                        wrong.append(f"{name}: duty {got_duty:.3f} from the stream, the probe makes {made.duty / Pwm.FULL:.3f}")
                    if overruns:
                        wrong.append(f"{name}: {overruns} overruns")
                    # Over three quarters / under a quarter, as periph.adc: the probe's level is no reference.
                    if second == 1 and lo1 <= full * 3 / 4 or second == 0 and hi1 >= full / 4:
                        wrong.append(f"{name}: the second pin read {lo1}..{hi1}")
                    wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [line for line, _ in found], rate=1_000_000, steps=steps)
    finally:
        pwm.stop(0)
        core.plan_release(host, [pwm.fn])
        if rest:
            gpio.configure(rest[0][0]["channel"], Gpio.INPUT)
            core.plan_release(host, [gpio.fn])
    record_property("results", json.dumps(results))
    assert not wrong, " | ".join(wrong)


HSADC_SAFE_CHANNELS = (4, 5, 6)     # PF8-PF10, VDDIO pads; PC0-PC3 (0-3) are VIO18's, rated to VIO18 + 0.3 V


def test_hsadc(dut, oep_host, ws_run, record_property):
    """periph.hsadc (H41x): CH32RVHsAdc on a labelled pad that is an HSADC input on VDDIO (PF8-PF10 - never PC0-PC3,
    whose inputs are rated to VIO18 + 0.3 V and VIO18 may be below the probe's 3.3 V), the probe's PWM on it. The
    sketch reads 8 buffers of 1024 samples and reports the square wave as periph.adc-stream does; one capture, a
    WireSkein section per condition judging the stimulus (becomes); compared here: the frequency from the rises
    within 3 % of the probe's, the duty within 0.03, no overrun - at 20 Msps with 100 kHz / 0.5 and 1 MHz / 0.25,
    and at the slowest step (1.25 Msps) with 50 kHz / 0.75. Prepared before an H41x on the bench had labelled lines
    (2026-11-05)."""
    from oep_client import core
    from oep_client.fixture import Pwm
    from wireskein.runlog import becomes
    case = settings(__file__)
    ready(dut, case)
    line = None
    for candidate in gpio_lines(oep_host, case["target"]):
        channel = int(ask(dut, f"hscaps {candidate['pin']}", rb"HSCAPS (-?\d+)").group(1))
        if channel in HSADC_SAFE_CHANNELS:
            line = candidate
            break
    if line is None:
        pytest.skip("no labelled <port>@<pad> line is an HSADC input on VDDIO (PF8-PF10)")
    record_property("line", f"{line['pad']} channel {channel}")
    host = oep_host.host
    pwm = Pwm(host)
    core.plan_apply(host, pwm.assignments(line["channel"]))
    conditions = [(20_000_000, 100_000, 0.5), (20_000_000, 1_000_000, 0.25), (1_250_000, 50_000, 0.75)]
    results, wrong = {}, []

    def steps(wait):
        with ws_run.section(1, f"hsadc {line['pad']}"):
            for rate, hz, duty in conditions:
                name = f"{rate // 1000} ksps, {hz // 1000} kHz duty {duty}"
                with ws_run.section(2, name, expect=[becomes(line["pad"], freq_hz=hz, duty=duty, irregular_max=3,
                                                             min_periods=20, duty_abs=0.01)]):
                    made = pwm.start(0, hz, round(duty * Pwm.FULL))
                    wait(0.02)
                    command = f"hstream {rate} 8 {line['pin']}"
                    ws_run.command(command)
                    dut.write(command + "\n")
                    end = time.monotonic() + 5
                    while True:
                        try:
                            m = dut.expect(rb"HSTREAM ([\d ]+)\r?\n", timeout=0.05)
                            break
                        except Exception:
                            assert time.monotonic() < end, f"{name}: no HSTREAM"
                            wait(0.05)
                    ws_run.reply(m.group(0).decode())
                    v = [int(x) for x in m.group(1).split()]
                    if v[0] == 0:
                        wrong.append(f"{name}: begin() refused")
                        continue
                    got_rate, overruns, n, high, low, rises, first, last = v
                    got_hz = got_rate * (rises - 1) / (last - first) if rises > 1 and last > first else 0.0
                    got_duty = high / (high + low) if high + low else 0.0
                    results[name] = dict(rate=got_rate, hz=round(got_hz, 1), duty=round(got_duty, 4),
                                         overruns=overruns, probe_hz=made.hz)
                    if abs(got_hz / made.hz - 1) > 0.03:
                        wrong.append(f"{name}: {got_hz:.1f} Hz from the stream, the probe makes {made.hz:.1f}")
                    if abs(got_duty - made.duty / Pwm.FULL) > 0.03:
                        wrong.append(f"{name}: duty {got_duty:.3f}, the probe makes {made.duty / Pwm.FULL:.3f}")
                    if overruns:
                        wrong.append(f"{name}: {overruns} overruns")
                    wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [line], rate=10_000_000, steps=steps)
    finally:
        pwm.stop(0)
        core.plan_release(host, [pwm.fn])
    record_property("results", json.dumps(results))
    assert not wrong, " | ".join(wrong)


@pytest.mark.parametrize("feature", ["can", "i2s", "argb", "rs485", "qspi"])
def test_planned(dut, oep_host, feature):
    """Planned features: the names are fixed, the code is not written (FEATURE_CANDIDATES.ja.md)."""
    case = settings(__file__)
    lines = require(oep_host, case["target"], feature)
    pytest.fail(f"{feature}: the bench labels {', '.join(sorted(lines))}, but the core has no {feature} driver yet")
