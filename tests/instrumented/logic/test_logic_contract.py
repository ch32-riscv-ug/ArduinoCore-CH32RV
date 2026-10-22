"""logic.continuous / logic.burst: CH32RVLogicCapture on a port of the DUT's own
(tests/sketch_support/logic/logic.ino). The sketch outputs a PWM from another
timer on a pin of that port; with the capture at 1 MS/s and the PWM at 10 kHz
(250 kS/s and 2.5 kHz with the small parts' 512-sample buffer), both from
F_CPU, every period is 100 samples. No wiring."""
import json
import pathlib
import secrets


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def test_logic_contract(dut, record_property):
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d+) (\d+)", timeout=15)
    assert m.group(1).decode() == settings(__file__)["build_id"], "the DUT runs another build"
    record_property("capture_timer", int(m.group(2)))
    samples = int(m.group(3))
    rate = 1000000 if samples >= 2048 else 250000
    per_half = samples // 2 // 100        # whole periods a half can hold

    # logic.continuous. A pin from the bench file (logic_pin) or the sketch's own choice.
    pin = settings(__file__)["target"].get("logic_pin")
    if pin is not None:
        assert ask(dut, f"pin {pin}", rb"PIN \d+ (\d)").group(1) == b"1", f"pin {pin} has no PWM on another timer"
    m = ask(dut, f"run {rate} {rate // 100}",
            rb"LOGIC (?:NONE|FAIL \S+|(\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+))", timeout=15)
    assert m.group(1), m.group(0).decode()
    pin, port, bit, pwm_timer, got, count, lo, hi, overruns, halves, changed = (int(g) for g in m.groups())
    record_property("changed_bits", hex(changed))
    record_property("pwm_level", ask(dut, "level", rb"LEVEL [\d ]+").group(0).decode())
    record_property("pin", f"{pin} port {port} bit {bit} pwm TIM{pwm_timer}")
    assert got == rate
    assert halves == 8 and overruns == 0
    assert changed >> bit & 1, f"P{'ABCDEF'[port]}{bit} never changed: the PWM did not come out"
    assert count >= 8 * (per_half - 1), "the PWM's edges are missing"
    assert (lo, hi) == (100, 100), f"periods {lo}..{hi} samples, not 100"

    # logic.burst: as fast as the DMA goes (about F_CPU / 5) with the PWM at 100 kHz, so
    # the buffer holds several periods; they only need to be steady (the DMA's own pace)
    m = ask(dut, "burst 100000", rb"BURST (\d+) (\d+) (\d+) (\d+)", timeout=10)
    burst, count, lo, hi = (int(g) for g in m.groups())
    record_property("burst_rate", burst)
    assert burst > rate
    assert count >= 3 and hi - lo <= 4, f"burst periods {lo}..{hi} over {count}"
