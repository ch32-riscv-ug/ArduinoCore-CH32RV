# /// script
# requires-python = ">=3.13"
# dependencies = ["pyserial>=3.5", "wireskein>=0.0.2", "oep-client-python>=0.0.4"]
# ///
"""Coarse ADC check through the OEP probe (worklist P3 row 3, endpoints only): the P4 drives each
ADC-capable X035 pad (PA0..PA7, E143 wiring) push-pull low and high and analogRead() must read the
rails; floating input is reported for information. No calibrated mid-scale reference is available
on this fixture, so linearity is out of scope here. The second half decides whether channels
3/7/15 exist on this part at all (errata x035-adc-ch-i2c-unavailable): an absent channel reads the
previous conversion's residual charge, so it is judged by preconditioning, not by a single value.

  uv run tests/manual/oep_adc_trace/oep_adc_trace.py
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "oep_smoke"))
import oep_smoke  # noqa: E402
import targets  # noqa: E402
import trace_kit  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    targets.add_target_argument(parser)
    parser.add_argument("--port", help="probe serial port (default: the target profile's)")
    parser.add_argument("--fqbn", help="DUT board (default: the target profile's)")
    parser.add_argument("--order", help="comma separated pin order (default: the profile's ADC pins)")
    args = parser.parse_args()
    profile = targets.TARGETS[args.target]
    PIN_MAP = profile["adc"]
    CONSOLE_RX, CONSOLE_TX = profile["uart_rx"], profile["uart_tx"]

    log = print
    session = trace_kit.Session(args.port, profile, "adc_probe", log, source=HERE / "adc_probe", fqbn=args.fqbn,
                                defines=targets.build_defines(profile))
    FixtureGpio = trace_kit.Gpio
    gpio = trace_kit.Gpio(session)   # each pin joins the gpio plan when first driven
    failures = []
    try:
        link = session.link
        if not oep_smoke.sync(link, "adc_probe READY"):
            raise SystemExit(f"no READY: {link.text[:200]!r}")

        def adc(name):
            link.drain(0.01); mark = len(link.text); link.send(f"ADC {name} 16\n")
            if not link.wait(f"ADC {name} n=", 3): raise SystemExit(f"no reply for {name}")
            m = re.search(rf"ADC {name} n=\d+ min=(\d+) max=(\d+) mean=(\d+)", link.text[mark:])
            return tuple(int(x) for x in m.groups())

        def raw(ch, n=1000):   # regular-channel conversion bypassing the pin map (15 = VREFINT); (min, max, mean, first, last)
            link.drain(0.01); mark = len(link.text); link.send(f"CH {ch} {n}\n")
            if not link.wait(f"CH {ch} n=", 5): raise SystemExit(f"no reply for channel {ch}")
            m = re.search(rf"CH {ch} n=\d+ min=(\d+) max=(\d+) mean=(\d+) first=(\d+) last=(\d+)", link.text[mark:])
            return tuple(int(x) for x in m.groups())

        def seq(name, n=1000):   # first/last 8 of n back-to-back conversions
            link.drain(0.01); mark = len(link.text); link.send(f"ADCSEQ {name} {n}\n")
            if not link.wait(f"ADCSEQ {name} first=", 5): raise SystemExit(f"no reply for ADCSEQ {name}")
            return re.search(rf"ADCSEQ {name} (first=.*)", link.text[mark:]).group(1).strip()

        order = args.order.split(",") if args.order else list(PIN_MAP)
        for name in order:
            p4 = PIN_MAP[name]
            gpio.configure(p4, FixtureGpio.OUTPUT_LOW); time.sleep(0.01); low = adc(name)
            gpio.configure(p4, FixtureGpio.OUTPUT_HIGH); time.sleep(0.01); high = adc(name)
            gpio.configure(p4, FixtureGpio.INPUT_FLOATING); time.sleep(0.01); fl = adc(name)
            # Loose on purpose: this is "does the pad follow the rail", not a calibration - the absolute values are off
            # anyway, and the spread of 16 reads grew from <= 4 (2026-09-22) to 8-22 counts by 2026-09-29 with the old and
            # the new probe firmware alike (outside the firmware). A pin that does not follow still fails by hundreds.
            ok = low[1] <= 48 and high[0] >= profile["adc_high_min"] and (low[1] - low[0]) <= 32 and (high[1] - high[0]) <= 32
            if not ok: failures.append(name)
            log(f"[{name} <- P4 GPIO{p4:2d}] low min/max/mean={low} high={high} floating={fl} -> {'OK' if ok else 'BAD'} (10-bit, rails: 0 V / P4 3.3 V)")
        # Rough mid-scale: P4 pull-up and pull-down together sit at 1.46-1.49 V (E087, pull-down a bit stronger),
        # i.e. about 455 of 1023 at 3.3 V. Not a calibrated reference; it shows the ADC is not just reading rails.
        mid_mode = FixtureGpio.INPUT_PULL_UP_DOWN
        mids = {}
        for name in order:
            if name in profile["adc_absent"]: continue
            gpio.configure(PIN_MAP[name], mid_mode); time.sleep(0.02); mids[name] = adc(name)
            gpio.configure(PIN_MAP[name], FixtureGpio.INPUT_FLOATING)
        log("[mid-scale, P4 pull-up+pull-down ~1.47 V -> expect ~430..480] " + " ".join(f"{n}={v[2]}" for n, v in mids.items()))
        if args.target != "x035":
            log(f"failures={failures}"); return
        # Errata x035-adc-ch-i2c-unavailable (CH32X035DS0 note 1): ADC channels 3/7/11/15 are absent on lots whose
        # fifth-to-last lot-number digit is 0. A missing channel is not "reads 0": the sample-and-hold node keeps the
        # charge of the previous conversion and decays a few percent per 1000 conversions, so a single read looks
        # plausible. Channel 15 is VREFINT (1.2 V typ -> ~372 of 1023 at 3.3 V) and needs no wiring: precondition the
        # node with PA0 at each rail and read ch15 1000 times; a present channel snaps to ~372 both times, an absent
        # one tracks the rail it was preconditioned with.
        vref = []
        for level in (FixtureGpio.OUTPUT_HIGH, FixtureGpio.OUTPUT_LOW):
            gpio.configure(PIN_MAP["PA0"], level); time.sleep(0.01); pre = adc("PA0"); v = raw(15)
            vref.append(v)
            log(f"[VREFINT ch15 after PA0={pre[2]:4d}] min/max/mean/first/last={v} over 1000 conversions (expect ~372 both times)")
        gpio.configure(PIN_MAP["PA0"], FixtureGpio.INPUT_FLOATING)
        vref_ok = all(330 <= v[2] <= 420 and abs(v[3] - v[4]) <= 8 for v in vref)
        for name, ch in (("PA3", 3), ("PA7", 7)):
            p4 = PIN_MAP[name]
            gpio.configure(p4, FixtureGpio.OUTPUT_HIGH); time.sleep(0.01)
            log(f"[{name} raw ch{ch}, P4 high] {raw(ch)}  ADCSEQ {seq(name)}")
            gpio.configure(p4, FixtureGpio.OUTPUT_LOW); time.sleep(0.01)
            log(f"[{name} raw ch{ch}, P4 low ] {raw(ch)}  ADCSEQ {seq(name)}")
            gpio.configure(p4, FixtureGpio.INPUT_FLOATING)
        log(f"VREFINT channel 15: {'PRESENT' if vref_ok else 'ABSENT (tracks the preconditioned node)'}")
        if not vref_ok: log("channels 3/7/11/15 read an isolated node: this part matches x035-adc-ch-i2c-unavailable (ADC clause)")
    finally:
        session.close()   # the gpio plan goes: every pin back to its idle state
    log(f"failures={failures}")


if __name__ == "__main__":
    main()
