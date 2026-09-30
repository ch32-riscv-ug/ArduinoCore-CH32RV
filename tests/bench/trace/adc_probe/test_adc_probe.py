"""analogRead() against the probe's rails, endpoints only: the probe drives each ADC pad the bench wires push-pull
low and high and the reading has to follow (loose bounds - the absolute values are off on this fixture, a pad that
does not follow fails by hundreds); a rough mid-scale from the probe's pull-up + pull-down for information; and on
the X035, whether ADC channels 3 / 7 / 15 exist on this part (errata x035-adc-ch-i2c-unavailable). An absent
channel is not "reads 0": its sample-and-hold node keeps the previous conversion's charge and decays a few percent
per 1000 conversions, so it is judged by preconditioning, not by one value.

  uv run --env-file .env pytest bench/trace/adc_probe --profile ch32x035 -s
"""
import re
import time

import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")
tk = load("tests/bench/tracekit.py", "tracekit")


@pytest.fixture
def fx(request, dut, bench, ws_run):
    pads = bench.facts.get("adc", [])
    if not pads:
        pytest.skip(f"{bench.name} names no ADC pads (facts.adc)")
    tk.need(bench, *pads)
    kit.start(dut, "adc_probe")
    con = tk.Console(dut, ws_run)
    oep_host = request.getfixturevalue("oep_host")
    f = tk.Fixture(oep_host.host, bench, ws_run, con)
    f.pads = pads
    yield f
    f.release()


def reading(con, verb: str, pin: int, n: int, extra: str = "") -> tuple[int, ...]:
    con.drain(0.01)
    line = con.ask(f"{verb} {pin} {n}{extra}", f"{verb} {pin} n=", 6)
    m = re.search(rf"{verb} {pin} n=\d+ min=(\d+) max=(\d+) mean=(\d+)(?: first=(\d+) last=(\d+))?", line)
    assert m, line
    return tuple(int(x) for x in m.groups() if x is not None)


def test_adc_follows_the_rails(fx, bench, ws_run):
    gpio, G = tk.Gpio(fx), tk.Gpio
    con = fx.console
    high_min = int(bench.facts.get("adc_high_min", 900))
    absent = set(bench.facts.get("adc_absent", []))
    bad = []
    for pad in fx.pads:
        pin = tk.encode(pad)
        gpio.only(pad)
        with ws_run.section(1, pad):
            gpio.configure(pad, G.OUTPUT_LOW); time.sleep(0.01); low = reading(con, "ADC", pin, 16)
            gpio.configure(pad, G.OUTPUT_HIGH); time.sleep(0.01); high = reading(con, "ADC", pin, 16)
            gpio.configure(pad, G.INPUT_FLOATING); time.sleep(0.01); floating = reading(con, "ADC", pin, 16)
        # The spread of 16 reads is 8..22 counts on this fixture (2026-09-29), so 32; the rails by hundreds.
        ok = low[1] <= 48 and high[0] >= high_min and (low[1] - low[0]) <= 32 and (high[1] - high[0]) <= 32
        if pad in absent:                       # no channel behind this pad on this part: reported, not judged
            print(f"[{pad} <- probe {bench.channel(pad):2d}] low={low} high={high} floating={floating} "
                  f"(channel absent on this part, errata: not judged)")
            continue
        print(f"[{pad} <- probe {bench.channel(pad):2d}] low min/max/mean={low} high={high} floating={floating} "
              f"-> {'OK' if ok else 'BAD'} (10-bit, rails 0 V / probe 3.3 V)")
        if not ok:
            bad.append(pad)
    # Rough mid-scale: the probe's pull-up and pull-down together sit near 1.47 V (about 455 of 1023 at 3.3 V).
    # Not a calibrated reference; it shows the ADC is not just reading rails.
    mids = {}
    for pad in fx.pads:
        if pad in absent:
            continue
        gpio.only(pad)
        gpio.configure(pad, G.INPUT_PULL_UP_DOWN); time.sleep(0.02); mids[pad] = reading(con, "ADC", tk.encode(pad), 16)[2]
        gpio.configure(pad, G.INPUT_FLOATING)
    print("[mid-scale, probe pull-up + pull-down ~1.47 V -> expect ~430..480] " + " ".join(f"{p}={v}" for p, v in mids.items()))
    ws_run.note("mid-scale " + " ".join(f"{p}={v}" for p, v in mids.items()))
    assert not bad, f"pads that do not follow the rails: {bad}"


def test_x035_absent_channels(fx, bench, ws_run):
    """Errata x035-adc-ch-i2c-unavailable (CH32X035DS0 note 1): ADC channels 3 / 7 / 11 / 15 are absent on lots
    whose fifth-to-last lot-number digit is 0. Channel 15 is VREFINT (1.2 V typ, ~372 of 1023) and needs no wire:
    precondition the sample-and-hold node with the first ADC pad at each rail and read ch15 1000 times; a present
    channel snaps to ~372 both times, an absent one tracks the rail. The bench file says what this part is
    (facts.adc_absent), and the finding has to agree with it."""
    if bench.data["dut"]["board"].upper() != "CH32X035":
        pytest.skip("the errata is the X035's")
    gpio, G = tk.Gpio(fx), tk.Gpio
    con = fx.console
    first = fx.pads[0]
    setup_pin = tk.encode(first)
    vref = []
    for level in (G.OUTPUT_HIGH, G.OUTPUT_LOW):
        gpio.configure(first, level); time.sleep(0.01)
        pre = reading(con, "ADC", setup_pin, 16)
        v = reading(con, "CH", 15, 1000, f" {setup_pin}")
        vref.append(v)
        print(f"[VREFINT ch15 after {first}={pre[2]:4d}] min/max/mean/first/last={v} over 1000 conversions (present: ~372 both times)")
    gpio.configure(first, G.INPUT_FLOATING)
    vref_present = all(330 <= v[2] <= 420 and abs(v[3] - v[4]) <= 8 for v in vref)
    absent = set(bench.facts.get("adc_absent", []))
    for pad, ch in (("PA3", 3), ("PA7", 7)):
        if pad not in fx.pads:
            continue
        pin = tk.encode(pad)
        for level, name in ((G.OUTPUT_HIGH, "high"), (G.OUTPUT_LOW, "low")):
            gpio.configure(pad, level); time.sleep(0.01)
            raw = reading(con, "CH", ch, 1000, f" {setup_pin}")
            seq = con.ask(f"ADCSEQ {pin} 1000", f"ADCSEQ {pin} first=", 6)
            print(f"[{pad} raw ch{ch}, probe {name}] {raw}  {seq}")
        gpio.configure(pad, G.INPUT_FLOATING)
    verdict = "PRESENT" if vref_present else "ABSENT (tracks the preconditioned node)"
    print(f"VREFINT channel 15: {verdict}")
    ws_run.note(f"VREFINT ch15 {verdict}")
    assert vref_present == (not absent), (
        f"the bench file says adc_absent={sorted(absent)} but channel 15 is {verdict}: the part or the file changed")
