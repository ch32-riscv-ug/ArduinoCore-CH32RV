"""analog (case analog): the DAC through analogWrite() (tests/sketch_support/analog/analog.ino), no wiring.

analog.dac: on each pad the variant names as a DAC output (CH32RV_DAC1_PIN / CH32RV_DAC2_PIN), analogWrite() at
0, 1/4, 1/2, 3/4 and full scale, with the default 8-bit resolution and with analogWriteResolution(12), each read
back by analogRead() on the same pad eight times: the mean within 4 % of the ADC's full scale of the value written
(the DAC's and the ADC's offsets and gains together, on the chip's own supply), the eight readings within 1 % of
each other (a steady output). A part without a DAC skips.

Prepared before a bench target had a DAC that this suite could reach (2026-11-05): V30x / V317 / V4x7 / H41x."""
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
        pytest.skip("the part has no DAC output")
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
