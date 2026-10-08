"""Every DUT pad the bench wires to the probe, from both sides: the DUT drives and the probe reads; the DUT's
open-drain release can be pulled by the probe; the probe drives and the DUT reads in INPUT / INPUT_PULLUP /
INPUT_PULLDOWN; EXTI RISING / FALLING / CHANGE count the probe's pulses. One loop over the pads, failures
collected, so the report names every bad pad at once.

The pads come from the bench file's [wiring] minus the UART's; the sketch takes pin numbers, so no jig is in it.

  uv run --env-file .env pytest bench/trace/gpio_probe --profile ch32x035 -s
"""
import time

import pytest
from loader import load

kit = load("tests-legacy/bench/bench_kit.py", "bench_kit")
tk = load("tests-legacy/bench/tracekit.py", "tracekit")

SETTLE = 0.05      # seconds between a drive change and the sample (some probe pins release slowly)


def matrix_pads(bench) -> list[str]:
    uart = bench.data.get("uart", {})
    keep_out = {uart.get("tx"), uart.get("rx")}
    return [p for p, ch in bench.wiring.items() if isinstance(ch, int) and p not in keep_out]


def test_gpio_matrix(request, dut, bench, ws_run):
    pads = matrix_pads(bench)
    if not pads:
        pytest.skip(f"{bench.name} wires no pad for the GPIO matrix")
    kit.start(dut, "gpio_probe")
    con = tk.Console(dut, ws_run)
    oep_host = request.getfixturevalue("oep_host")
    fx = tk.Fixture(oep_host.host, bench, ws_run, con)
    try:
        gpio = tk.Gpio(fx)
        G = tk.Gpio
        pullup = set(bench.facts.get("external_pullup", []))
        slow = set(bench.facts.get("slow_release", []))
        pulled_low = set(bench.facts.get("pulled_low", []))     # pads the DUT itself holds low (the X035's CC pins)
        bad = []
        for pad in pads:
            pin = tk.encode(pad)
            gpio.only(pad)                                      # one pad in the plan at a time

            def cmd(text, reply):
                con.drain(0.01)
                return con.ask(text, reply, 3)

            def dut_read():
                return int(cmd(f"READ {pin}", "READ v=").split("v=")[1])

            with ws_run.section(1, pad):
                row = {}
                # 1. DUT drives, probe samples (probe input floating)
                gpio.configure(pad, G.INPUT_FLOATING)
                cmd(f"MODE {pin} 3", "MODE ok")
                cmd(f"WRITE {pin} 0", "WRITE ok"); time.sleep(SETTLE); lo = gpio.read(pad)
                cmd(f"WRITE {pin} 1", "WRITE ok"); time.sleep(SETTLE); hi = gpio.read(pad)
                row["out"] = (lo, hi)
                # 1b. DUT open-drain: released must be pullable by the probe
                cmd(f"MODE {pin} 4", "MODE ok"); cmd(f"WRITE {pin} 1", "WRITE ok")
                gpio.configure(pad, G.OUTPUT_LOW); time.sleep(SETTLE); od_pulled = dut_read()
                gpio.configure(pad, G.INPUT_PULL_UP); time.sleep(SETTLE); od_released = dut_read()
                cmd(f"WRITE {pin} 0", "WRITE ok"); time.sleep(SETTLE); od_low = gpio.read(pad)
                row["od"] = (od_pulled, od_released, od_low)
                gpio.configure(pad, G.INPUT_FLOATING)
                # 2. probe drives, DUT samples in each input mode
                for m, mname in ((0, "input"), (1, "pullup"), (2, "pulldown")):
                    cmd(f"MODE {pin} {m}", "MODE ok")
                    gpio.configure(pad, G.INPUT_FLOATING); time.sleep(SETTLE); idle = dut_read()
                    gpio.configure(pad, G.OUTPUT_LOW); time.sleep(SETTLE); low = dut_read()
                    gpio.configure(pad, G.OUTPUT_HIGH); time.sleep(SETTLE); high = dut_read()
                    gpio.configure(pad, G.INPUT_FLOATING)
                    row[mname] = (idle, low, high)
                # 3. EXTI counts: the probe toggles 10 pulses
                cmd(f"MODE {pin} 0", "MODE ok")
                gpio.configure(pad, G.OUTPUT_LOW); time.sleep(SETTLE)
                exti = {}
                for m, mname, expect in ((0, "rising", 10), (1, "falling", 10), (2, "change", 20)):
                    cmd(f"EXTI {pin} {m}", "EXTI armed")
                    for _ in range(10):
                        gpio.configure(pad, G.OUTPUT_HIGH)
                        gpio.configure(pad, G.OUTPUT_LOW)
                    time.sleep(0.01)
                    count = int(cmd("COUNT", "COUNT n=").split("n=")[1])
                    cmd(f"EXTIOFF {pin}", "EXTIOFF ok")
                    exti[mname] = (count, expect)
                gpio.configure(pad, G.INPUT_FLOATING)
                cmd(f"MODE {pin} 0", "MODE ok")
            # A pad the DUT holds low itself (pulled_low) cannot float high: its open-drain release reads 0 even
            # with the probe's pull-up, and INPUT_PULLUP idles 0. Everything driven is still checked.
            weak = pad in pulled_low
            ok = (row["out"] == (0, 1)
                  and (row["od"] == (0, 1, 0) or (weak and row["od"][0] == 0 and row["od"][2] == 0))
                  and row["input"][1:] == (0, 1)
                  and (row["pullup"] == (1, 0, 1) or (weak and row["pullup"][1:] == (0, 1)))
                  and row["pulldown"][1:] == (0, 1)
                  and (row["pulldown"][0] == 0 or pad in slow or pad in pullup)
                  and all(c == e for c, e in exti.values()))
            print(f"[{pad:5s} <-> probe {bench.channel(pad):2d}] {'OK ' if ok else 'BAD'} out={row['out']} od={row['od']} "
                  f"in={row['input']} pu={row['pullup']} pd={row['pulldown']} "
                  f"exti r/f/c={exti['rising'][0]}/{exti['falling'][0]}/{exti['change'][0]}")
            if not ok:
                bad.append((pad, row, exti))
                ws_run.note(f"{pad}: {row} exti={exti}")
        assert not bad, "pads wrong from one side or the other: " + ", ".join(p for p, _, _ in bad)
    finally:
        fx.release()
