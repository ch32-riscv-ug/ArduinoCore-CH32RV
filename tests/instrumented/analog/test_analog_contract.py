"""analog (case analog): the DAC through analogWrite() (tests/sketch_support/analog/analog.ino), no wiring.

analog.dac: on each pad the variant names as a DAC output (CH32RV_DAC1_PIN / CH32RV_DAC2_PIN), analogWrite() at
0, 1/4, 1/2, 3/4 and full scale, with the default 8-bit resolution and with analogWriteResolution(12), each read
back by analogRead() on the same pad eight times: the mean within 4 % of the ADC's full scale of the value written
(the DAC's and the ADC's offsets and gains together, on the chip's own supply), the eight readings within 1 % of
each other (a steady output). A part without a DAC skips.

The series with a DAC: V30x / V317 / V4x7 / H41x."""
import json
import pathlib
import secrets

import pytest


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def test_dac(dut, record_property):
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    pins = [int(p) for p in ask(dut, "dacpins", rb"DACPINS (-?\d+) (-?\d+)").groups() if int(p) >= 0]
    if not pins:
        pytest.skip("n/a: the part has no DAC output")
    readings, wrong = {}, []
    for pin in pins:
        for bits in (8, 12):
            top = (1 << bits) - 1
            for share in (0, 0.25, 0.5, 0.75, 1):
                value = round(top * share)
                mean, lo, hi, full = (int(x) for x in ask(dut, f"dac {pin} {value} {bits}",
                                                          rb"DAC (\d+) (\d+) (\d+) (\d+)").groups())
                want = value / top * full
                readings[f"{pin}/{bits}/{value}"] = [mean, lo, hi]
                if abs(mean - want) > 0.04 * full:
                    wrong.append(f"pin {pin} {bits}-bit {value}: read {mean} of {full}, want about {want:.0f}")
                if hi - lo > 0.01 * full:
                    wrong.append(f"pin {pin} {bits}-bit {value}: readings {lo}..{hi} do not hold steady")
    record_property("readings", json.dumps(readings))
    assert not wrong, " | ".join(wrong)


# (mode, bits, steps a second, base): a triangle of 2046 steps (10 ms), noise, and a 32-sample ramp at 1 kHz.
WAVES = [("t", 10, 200_000, 1024), ("n", 8, 100_000, 2048), ("p", 10, 32_000, 1024)]


def test_dac_waves(dut, record_property):
    """CH32RVDac on each DAC channel, read back by analogRead() on its own pad (no wiring): the converter's triangle
    (10 bits above 1024), its noise (8 bits above 2048) and a 32-sample ramp played by DMA, each for 200 reads 100 us
    apart while it runs. The lowest and highest reads within 4 % of the ADC's full scale of the wave's bottom and top
    (as test_dac judges a steady value; the reads may miss the very ends, so the band is measured inwards only), the
    reads changing more than 50 times (it moves), and the step rate within 1 % of the one asked for."""
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    pins = ask(dut, "dacpins", rb"DACPINS (-?\d+) (-?\d+)").groups()
    channels = [k + 1 for k, p in enumerate(pins) if int(p) >= 0]
    if not channels:
        pytest.skip("n/a: the part has no DAC output")
    results, wrong = {}, []
    for channel in channels:
        for mode, bits, hz, base in WAVES:
            m = ask(dut, f"dacwave {channel} {mode} {bits} {hz} {base}",
                    rb"DACWAVE (\d)(?: (\d+) (\d+) (\d+) (\d+) (\d+) (\d+))?")
            name = f"DAC{channel} {mode}"
            if m.group(1) != b"1":
                wrong.append(f"{name}: refused")
                continue
            rate, lo, hi, mean, changes, full = (int(x) for x in m.groups()[1:])
            results[name] = dict(rate=rate, lo=lo, hi=hi, mean=mean, changes=changes)
            bottom, top = base / 4095 * full, (base + (1 << bits) - 1) / 4095 * full
            if lo < bottom - 0.04 * full or lo > bottom + 0.04 * full + 0.25 * (top - bottom):
                wrong.append(f"{name}: lowest read {lo}, the wave's bottom about {bottom:.0f}")
            if hi > top + 0.04 * full or hi < top - 0.04 * full - 0.25 * (top - bottom):
                wrong.append(f"{name}: highest read {hi}, the wave's top about {top:.0f}")
            if changes <= 50:
                wrong.append(f"{name}: the reads changed {changes} times in 200: not moving")
            if abs(rate - hz) > 0.01 * hz:
                wrong.append(f"{name}: {rate} steps a second, asked {hz}")
    record_property("waves", json.dumps(results))
    assert not wrong, " | ".join(wrong)


# (DAC1's value, DAC2's value, timer rate or 0 free-running)
DUAL_SIMULTANEOUS = [(1024, 3072, 0), (3072, 1024, 10_000)]
# DAC1's square: 0 and 4095 alternately, each held 1 / DUAL_SQUARE_HZ
DUAL_SQUARE_HZ = 50_000


def test_adc_dual(dut, record_property):
    """CH32RVAdcStream's dual mode, read on the DAC pads (no wiring). Simultaneous: DAC1 and DAC2 hold two values,
    ADC1 converts DAC1's pad and ADC2 DAC2's in the same rounds, free-running and on a 10 kHz timer: each side's mean
    within 4 % of full scale of its own DAC (test_dac's band) and steady within 1 % - a swapped or shifted pair would
    put the other value there; the timer's rate within 1 %. Interleaved: DAC1 plays a square (each level held
    1 / 50 kHz), both ADCs in turn on its pad: cut at mid-scale, every whole run as long as the level lasts at the
    rate sampleRate() reports, within 20 % (pairs out of time order would cut runs of 1 or 2 samples), and that rate
    1.6 .. 2.4 times one ADC's alone at the same step. A series without the second ADC or the DACs skips."""
    case = settings(__file__)
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+)", timeout=15)
    assert m.group(1).decode() == case["build_id"], "the DUT runs another build"
    probe = ask(dut, "adcdual s 0 0 0", rb"(ADCDUAL [^\r\n]*|UNKNOWN adcdual)")
    if probe.group(1).startswith(b"UNKNOWN"):
        pytest.skip("n/a: no dual ADC (CH32RV_ADC_DUALMOD_SHIFT) or no two DAC pads")
    results, wrong = {}, []
    for v1, v2, hz in DUAL_SIMULTANEOUS:
        name = f"simultaneous {v1}/{v2} " + (f"timer {hz}" if hz else "free")
        m = ask(dut, f"adcdual s {v1} {v2} {hz}", rb"ADCDUAL (\d)((?: \d+){7})?")
        if m.group(1) != b"1":
            wrong.append(f"{name}: refused or no data")
            continue
        rate, m1, lo1, hi1, m2, lo2, hi2 = (int(x) for x in m.group(2).split())
        results[name] = dict(rate=rate, adc1=[m1, lo1, hi1], adc2=[m2, lo2, hi2])
        full = 4095
        for side, want, mean, lo, hi in (("ADC1", v1, m1, lo1, hi1), ("ADC2", v2, m2, lo2, hi2)):
            if abs(mean - want) > 0.04 * full:
                wrong.append(f"{name}: {side} read {mean}, its DAC holds {want}")
            if hi - lo > 0.01 * full:
                wrong.append(f"{name}: {side} {lo}..{hi} does not hold steady")
        if hz and abs(rate - hz) > 0.01 * hz:
            wrong.append(f"{name}: {rate} rounds a second, asked {hz}")
    m = ask(dut, f"adcdual i {DUAL_SQUARE_HZ}", rb"ADCDUAL (\d)((?: \d+){5})?")
    if m.group(1) != b"1":
        wrong.append("interleaved: refused or no data")
    else:
        rate, runs, shortest, longest, single = (int(x) for x in m.group(2).split())
        results["interleaved"] = dict(rate=rate, runs=runs, shortest=shortest, longest=longest, single=single)
        want = rate / DUAL_SQUARE_HZ
        if runs < 3:
            wrong.append(f"interleaved: {runs} whole runs in 1024 samples at {rate}/s")
        elif shortest < 0.8 * want or longest > 1.2 * want:
            wrong.append(f"interleaved: runs {shortest}..{longest} samples, the level lasts {want:.1f}")
        if not single or not 1.6 <= rate / single <= 2.4:
            wrong.append(f"interleaved: {rate} samples a second against {single} for one ADC")
    record_property("dual", json.dumps(results))
    assert not wrong, " | ".join(wrong)
