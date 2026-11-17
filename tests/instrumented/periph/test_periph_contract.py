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
- periph.adc-timer-trigger: CH32RVAdcStream.beginTimer() on such a pad, the probe's PWM on it
  (test_adc_timer_trigger).
- periph.adc-external-trigger: CH32RVAdcStream.beginExternal() on such a pad, its trigger a labelled pad the DUT
  names as the ADC's trigger pad, the probe pulsing it (test_adc_external_trigger).
- periph.analog-continuous: arduino-esp32's analogContinuous() on such a pad (test_analog_continuous).
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
    # A route on the console's pads is left out, not its USART: the sketch's console is SerialDMSeq, so the
    # USART whose other route reaches those pads is free, and begin(rx, tx) may take it (V205: USART1 on PA6 / PA7).
    pairs = {}                      # (tx, rx): [(usart, route), ...] - one section per pad pair
    for n, k, tx, rx in routes:
        if not {tx, rx} & console and tx in lines and rx in lines:
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
    of the probe's, its duty within 0.03 of the probe's, no overrun, and the second pin at its level (over 3/4 of full scale
    high, under 1/4 low) in every round but at most one per edge of the wave: a held line beside the wave can dip on
    its edges (6-2's harness, 2026-11-14: about one round in 150 between the thresholds, as often with the pin read
    alone; the range is recorded)."""
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
        # Driven from here on: after plan_apply the probe's pin is a floating input and the DUT's an analog one,
        # so until the first condition set it the line picked up the PWM beside it and the ADC's sampling - tens
        # of pulses of neither level before its change (5-1 and 5-2, 2026-11-14).
        gpio.configure(rest[0][0]["channel"], Gpio.OUTPUT_LOW)
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
                    rate, step, overruns, rounds, high, low, rises, first, last, lo1, hi1, full, high1n, low1n = v
                    got_hz = rate * (rises - 1) / (last - first) if rises > 1 and last > first else 0.0
                    got_duty = high / (high + low) if high + low else 0.0
                    results[name] = dict(rate=rate, step=step, rounds=rounds, hz=round(got_hz, 2),
                                         duty=round(got_duty, 4), overruns=overruns, second=[lo1, hi1],
                                         second_off=rounds - (high1n if second == 1 else low1n) if second is not None else 0,
                                         probe_hz=made.hz, probe_duty=made.duty / Pwm.FULL)
                    if abs(got_hz / made.hz - 1) > 0.03:
                        wrong.append(f"{name}: {got_hz:.1f} Hz from the stream, the probe makes {made.hz:.1f}")
                    if abs(got_duty - made.duty / Pwm.FULL) > 0.03:
                        wrong.append(f"{name}: duty {got_duty:.3f} from the stream, the probe makes {made.duty / Pwm.FULL:.3f}")
                    if overruns:
                        wrong.append(f"{name}: {overruns} overruns")
                    # Over three quarters / under a quarter, as periph.adc: the probe's level is no reference.
                    if second is not None:
                        off = rounds - (high1n if second == 1 else low1n)
                        edges = 2 * rises + 2
                        if off > edges:
                            wrong.append(f"{name}: the second pin left its level in {off} of {rounds} rounds "
                                         f"(read {lo1}..{hi1}; the wave has about {edges} edges)")
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


def adc_pads(dut, oep_host, case, n, skip_pads=()):
    """Up to n labelled lines the DUT names as ADC channels (not the record's defects', not skip_pads):
    [(line, channel)]."""
    lost = {int(f.split(":")[1]) for d in case["target"].get("record", {}).get("defects", [])
            for f in d.get("lose", []) if f.startswith("adc:")}
    found = []
    for candidate in gpio_lines(oep_host, case["target"]):
        if candidate["pad"] in skip_pads:
            continue
        channel = int(ask(dut, f"acaps {candidate['pin']}", rb"ACAPS (-?\d+)").group(1))
        if channel >= 0 and channel not in lost:
            found.append((candidate, channel))
        if len(found) == n:
            break
    return found


def built(dut, command):
    """Skip unless this periph build holds `command` (the DUT says, has: a build for a small flash leaves some out,
    periph.ino)."""
    m = ask(dut, "has", rb"HAS (\d) (\d) (\d)")
    held = dict(zip(("atimer", "aext", "acont"), (g == b"1" for g in m.groups())))
    if not held[command]:
        pytest.skip(f"{command} is not in this periph build (the DUT says so: has; periph.ino leaves it out of a "
                    "16 KB flash)")


def until(dut, ws_run, wait, command, reply, seconds):
    """Send command, record it and its reply in the run, letting the stream be read meanwhile."""
    ws_run.command(command)
    dut.write(command + "\n")
    end = time.monotonic() + seconds
    while True:
        try:
            m = dut.expect(reply + rb"\r?\n", timeout=0.05)
            break
        except Exception:
            assert time.monotonic() < end, f"no reply to {command!r}"
            wait(0.05)
    ws_run.reply(m.group(0).decode())
    return m


def test_adc_timer_trigger(dut, oep_host, ws_run, record_property):
    """periph.adc-timer-trigger: CH32RVAdcStream.beginTimer() - one conversion per tick of a timer the core takes from
    the family's ADC trigger sources - on a labelled ADC pad, the probe's PWM on it at 1/40 of the rate (40 samples a
    period), at 20 kHz and at 2 kHz (below every family's free-running floor: 47,600 a second on V307 is the lowest
    measured, CH32RVAdcStream's README). One capture of the line, a WireSkein section per condition judging the
    stimulus (becomes); compared here, from what the DUT reports (ATIMER):
    - the rate the timer made (sampleRate(): its clock over its divisions) within 0.1 % of the rate asked: a timer
      clock of 48 MHz or more divides 20 kHz into 2,400 ticks at least, so the rounding of the divisions is 0.02 %;
    - the conversions the DUT counted (conversions()) over its own micros() between two halves' ends within 0.5 % +
      2 conversions of that rate: both on the DUT's clock, so only the window's ends (a poll of readBuffer(), tens of
      microseconds in at least 0.2 s) and the count's ends move it - a timer that does not trigger every round, or
      triggers more, is far outside;
    - the probe's wave seen in the samples: its frequency (rate x rises / rounds between the first and last rise)
      within 3 % of the probe's (the DUT's clock against the probe's, as adc-stream), its duty within 1/40 + 0.01
      (one sample of a period; the wave and the ticks keep nearly the same phase) of the probe's; no overrun.
    Skipped where the periph build leaves atimer out (the DUT says so: has)."""
    from oep_client import core
    from oep_client.fixture import Pwm
    from wireskein.runlog import becomes
    case = settings(__file__)
    ready(dut, case)
    built(dut, "atimer")
    found = adc_pads(dut, oep_host, case, 1)
    if not found:
        pytest.skip("no labelled <port>@<pad> line is an ADC channel")
    (wave, wave_ch), = found
    record_property("line", f"{wave['pad']} channel {wave_ch}")
    host = oep_host.host
    pwm = Pwm(host)
    core.plan_apply(host, pwm.assignments(wave["channel"]))
    conditions = [(20000, 0.5, 16), (2000, 0.25, 12)]      # (rate, the wave's duty, halves read)
    results, wrong = {}, []

    def steps(wait):
        with ws_run.section(1, f"adc timer trigger {wave['pad']}"):
            for rate, duty, halves in conditions:
                hz = rate / 40
                name = f"{rate} Hz, wave {hz:g} Hz duty {duty}"
                want = [becomes(wave["pad"], freq_hz=hz, duty=duty, irregular_max=3, min_periods=20, duty_abs=0.01)]
                with ws_run.section(2, name, expect=want):
                    made = pwm.start(0, hz, round(duty * Pwm.FULL))
                    wait(0.05)
                    m = until(dut, ws_run, wait, f"atimer {rate} {halves} {wave['pin']}", rb"ATIMER ([\d ]+)", 8)
                    v = [int(x) for x in m.group(1).split()]
                    if v[0] == 0:
                        wrong.append(f"{name}: beginTimer() refused")
                        continue
                    _, got, timer, step, overruns, rounds, high, low, rises, first, last, conv, us = v
                    got_hz = got * (rises - 1) / (last - first) if rises > 1 and last > first else 0.0
                    got_duty = high / (high + low) if high + low else 0.0
                    dut_rate = conv * 1e6 / us if us else 0.0
                    probe_duty = made.duty / Pwm.FULL
                    results[name] = dict(rate=got, timer=timer, step=step, rounds=rounds, conversions=conv, us=us,
                                         dut_rate=round(dut_rate, 2), hz=round(got_hz, 3), duty=round(got_duty, 4),
                                         overruns=overruns, probe_hz=made.hz, probe_duty=probe_duty)
                    if abs(got - rate) > rate * 0.001:
                        wrong.append(f"{name}: the timer made {got} Hz")
                    if not us or abs(dut_rate - got) > got * 0.005 + 2e6 / us:
                        wrong.append(f"{name}: {conv} conversions in {us} us ({dut_rate:.1f} a second) at {got} Hz")
                    if abs(got_hz / made.hz - 1) > 0.03:
                        wrong.append(f"{name}: {got_hz:.2f} Hz from the samples, the probe makes {made.hz:.2f}")
                    if abs(got_duty - probe_duty) > 1 / 40 + 0.01:
                        wrong.append(f"{name}: duty {got_duty:.3f} from the samples, the probe makes {probe_duty:.3f}")
                    if overruns:
                        wrong.append(f"{name}: {overruns} overruns")
                    wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [wave], rate=1_000_000, steps=steps)
    finally:
        pwm.stop(0)
        core.plan_release(host, [pwm.fn])
    record_property("results", json.dumps(results))
    assert not wrong, " | ".join(wrong)


EXT_PATTERN = [1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 0]


def test_adc_external_trigger(dut, oep_host, ws_run, record_property):
    """periph.adc-external-trigger: CH32RVAdcStream.beginExternal() - one conversion per edge of the family's
    trigger pad (any pad of bit 11 on most families, bit 12 on X305 / X315, PD3 / PC2 on V00x, PA14 / PB6 on M030:
    the DUT says, tcaps) - with a labelled trigger pad and a labelled ADC pad beside it. One capture of both lines,
    a WireSkein section per condition judging the stimulus: the trigger's 16 pulses (pulses), the PWM (becomes).
    - For RISING, FALLING and CHANGE: the probe sets the ADC pad to the next level of a 16-bit pattern, then pulses
      the trigger (high, low), 16 times. The DUT's conversions are exactly 16 (32 for CHANGE: both edges of a
      pulse) and their levels the pattern (each twice for CHANGE): one conversion per edge, of the level at it - an
      edge missed, a conversion more, or one at another time shows. Over 3/4 of full scale high, under 1/4 low, as
      adc.
    - The probe's PWM at 1 kHz on the trigger, the ADC pad high: the conversions the DUT counts over 500 ms of its
      micros() at the probe's frequency within 3 % (the DUT's clock against the probe's, as adc-stream), every one
      of the first 64 high."""
    from oep_client import core
    from oep_client.fixture import Gpio, Pwm
    from oep_client.host import Unavailable
    from wireskein.runlog import becomes, pulses
    case = settings(__file__)
    ready(dut, case)
    built(dut, "aext")
    trigger = None
    for candidate in gpio_lines(oep_host, case["target"]):
        if ask(dut, f"tcaps {candidate['pin']}", rb"TCAPS (\d)").group(1) == b"1":
            trigger = candidate
            break
    if trigger is None:
        pytest.skip("no labelled <port>@<pad> line is the ADC's trigger pad (the DUT names none, tcaps)")
    found = adc_pads(dut, oep_host, case, 1, skip_pads={trigger["pad"]})
    if not found:
        pytest.skip(f"no labelled <port>@<pad> line beside the trigger {trigger['pad']} is an ADC channel")
    (adc, adc_ch), = found
    record_property("lines", f"trigger {trigger['pad']}, ADC {adc['pad']} channel {adc_ch}")
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, trigger["channel"]), (gpio.fn, 1, adc["channel"])])
    gpio.configure(trigger["channel"], Gpio.OUTPUT_LOW)
    gpio.configure(adc["channel"], Gpio.OUTPUT_LOW)
    results, wrong = {}, []
    pwm = Pwm(host)

    def steps(wait):
        with ws_run.section(1, f"adc external trigger {trigger['pad']} -> {adc['pad']}"):
            for edge, name, per_pulse in ((4, "rising", 1), (3, "falling", 1), (2, "change", 2)):
                with ws_run.section(2, name, expect=[pulses(trigger["pad"], count=len(EXT_PATTERN))]):
                    m = until(dut, ws_run, wait, f"aext {trigger['pin']} {edge} {adc['pin']}", rb"AEXT (\d)", 5)
                    if m.group(1) != b"1":
                        wrong.append(f"{name}: beginExternal() refused")
                        continue
                    for bit in EXT_PATTERN:
                        gpio.configure(adc["channel"], Gpio.OUTPUT_HIGH if bit else Gpio.OUTPUT_LOW)
                        wait(0.002)
                        gpio.configure(trigger["channel"], Gpio.OUTPUT_HIGH)
                        wait(0.002)
                        gpio.configure(trigger["channel"], Gpio.OUTPUT_LOW)
                        wait(0.002)
                    wait(0.02)
                    m = until(dut, ws_run, wait, "aextread", rb"AEXTREAD (\d+) (\S+)", 5)
                    until(dut, ws_run, wait, "aextstop", rb"AEXTSTOP", 5)
                    n, levels = int(m.group(1)), m.group(2).decode()
                    want = "".join(("H" if bit else "L") * per_pulse for bit in EXT_PATTERN)
                    results[name] = dict(conversions=n, levels=levels, want=want)
                    if n != len(want) or levels != want:
                        wrong.append(f"{name}: {n} conversions reading {levels}, want {len(want)} reading {want}")
            gpio.configure(adc["channel"], Gpio.OUTPUT_HIGH)
            core.plan_release(host, [gpio.fn])
            core.plan_apply(host, pwm.assignments(trigger["channel"]))
            core.plan_apply(host, [(gpio.fn, 1, adc["channel"])])
            gpio.configure(adc["channel"], Gpio.OUTPUT_HIGH)
            want = [becomes(trigger["pad"], freq_hz=1000, duty=0.5, irregular_max=3, min_periods=20, duty_abs=0.02)]
            with ws_run.section(2, "rate 1 kHz", expect=want):
                m = until(dut, ws_run, wait, f"aext {trigger['pin']} 4 {adc['pin']} 1000", rb"AEXT (\d)", 5)
                made = pwm.start(0, 1000, Pwm.FULL // 2)
                wait(0.1)
                r = until(dut, ws_run, wait, "aextrate 500", rb"AEXTRATE (\d+) (\d+)", 5)
                lv = until(dut, ws_run, wait, "aextread", rb"AEXTREAD (\d+) (\S+)", 5)
                until(dut, ws_run, wait, "aextstop", rb"AEXTSTOP", 5)
                n, us = int(r.group(1)), int(r.group(2))
                got = n * 1e6 / us if us else 0.0
                levels = lv.group(2).decode()
                results["rate"] = dict(conversions=n, us=us, rate=round(got, 2), probe_hz=made.hz, levels=levels)
                if m.group(1) != b"1":
                    wrong.append("rate: beginExternal() refused")
                elif abs(got / made.hz - 1) > 0.03:
                    wrong.append(f"rate: {n} conversions in {us} us ({got:.1f} a second), the probe makes {made.hz:.1f} Hz")
                if levels.strip("H"):
                    wrong.append(f"rate: the ADC pad held high read {levels}")

    try:
        ws_capture.stream(ws_run, oep_host, [trigger, adc], rate=1_000_000, steps=steps)
    finally:
        try:
            pwm.stop(0)
        except Unavailable:      # the PWM was never assigned (the capture failed before the rate section)
            pass
        for fn in (pwm.fn, gpio.fn):
            try:
                core.plan_release(host, [fn])
            except Exception:
                pass
        gpio.configure(trigger["channel"], Gpio.INPUT)
        gpio.configure(adc["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])
    record_property("results", json.dumps(results))
    assert not wrong, " | ".join(wrong)


def test_analog_continuous(dut, oep_host, ws_run, record_property):
    """periph.analog-continuous: arduino-esp32's analogContinuous() on a labelled ADC pad at 10,000 conversions a
    second, frames of 100 (a frame every 10 ms), 10 frames waited for, the probe holding the pad low, then high; one
    capture of the line, a section per level (becomes: the level). Compared here, from what the DUT reports (ACONT):
    the frames counted (the callback) at least 10; the time between the first frame's callback and the last's,
    over the frames between them, within 0.5 % + 50 us of 10 ms (the timer's pace on the DUT's own clock: the
    callbacks' latency is microseconds); the average under 1/4 of full scale low and over 3/4 high (as adc), and
    its mV higher high than low (the mV's accuracy is a manual test). Skipped where the periph build leaves it
    out (the DUT says so: has)."""
    from oep_client import core
    from oep_client.fixture import Gpio
    from wireskein.runlog import becomes
    case = settings(__file__)
    ready(dut, case)
    built(dut, "acont")
    found = adc_pads(dut, oep_host, case, 1)
    if not found:
        pytest.skip("no labelled <port>@<pad> line is an ADC channel")
    (line, channel), = found
    record_property("line", f"{line['pad']} channel {channel}")
    host = oep_host.host
    gpio = Gpio(host)
    core.plan_apply(host, [(gpio.fn, 1, line["channel"])])
    gpio.configure(line["channel"], Gpio.OUTPUT_HIGH)
    results, wrong = {}, []

    def steps(wait):
        with ws_run.section(1, f"analog continuous {line['pad']}"):
            for level in (0, 1):
                with ws_run.section(2, f"level {level}", expect=[becomes(line["pad"], level=level)]):
                    gpio.configure(line["channel"], Gpio.OUTPUT_HIGH if level else Gpio.OUTPUT_LOW)
                    wait(0.02)
                    m = until(dut, ws_run, wait, f"acont 10000 100 10 {line['pin']}", rb"ACONT ([-\d ]+)", 8)
                    v = [int(x) for x in m.group(1).split()]
                    if v[0] == 0:
                        wrong.append(f"level {level}: analogContinuous() refused")
                        return
                    _, frames, span, raw, mv, full = v
                    period = span / (frames - 1) if frames > 1 else 0.0
                    results[level] = dict(frames=frames, span_us=span, period_us=round(period, 1), raw=raw, mv=mv,
                                          full=full)
                    if frames < 10:
                        wrong.append(f"level {level}: {frames} frames")
                    elif abs(period - 10000) > 10000 * 0.005 + 50:
                        wrong.append(f"level {level}: a frame every {period:.1f} us, want 10000")
                    if (level and raw <= full * 3 / 4) or (not level and raw >= full / 4):
                        wrong.append(f"level {level}: the average reads {raw} of {full}")
                    wait(0.02)

    try:
        ws_capture.stream(ws_run, oep_host, [line], rate=1_000_000, steps=steps)
    finally:
        gpio.configure(line["channel"], Gpio.INPUT)
        core.plan_release(host, [gpio.fn])
    record_property("results", json.dumps(results))
    if 0 in results and 1 in results and results[1]["mv"] <= results[0]["mv"]:
        wrong.append(f"mV high {results[1]['mv']} not above low {results[0]['mv']}")
    assert not wrong, " | ".join(wrong)


HSADC_SAFE_CHANNELS = (4, 5, 6)     # PF8-PF10, VDDIO pads; PC0-PC3 (0-3) are VIO18's, rated to VIO18 + 0.3 V


def test_hsadc(dut, oep_host, ws_run, record_property):
    """periph.hsadc (H41x): CH32RVHsAdc on a labelled pad that is an HSADC input on VDDIO (PF8-PF10 - never PC0-PC3,
    whose inputs are rated to VIO18 + 0.3 V and VIO18 may be below the probe's 3.3 V), the probe's PWM on it. The
    sketch reads 8 buffers of 1024 samples and reports the square wave as periph.adc-stream does; one capture, a
    WireSkein section per condition judging the stimulus (becomes); compared here: the frequency from the rises
    within 3 % of the probe's, the duty within 0.03, no overrun - at 20 Msps with 100 kHz / 0.5 and 1 MHz / 0.25,
    and at the slowest step (1.25 Msps) with 50 kHz / 0.75.
    The resident H417 has no line on an HSADC pad yet (TODO)."""
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
        # tests/README.ja.md 取り込みの速さ: T = 250 ns, the 1 MHz / 0.25 wave's high, 20 / T.
        ws_capture.stream(ws_run, oep_host, [line], rate=80_000_000, steps=steps)
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
