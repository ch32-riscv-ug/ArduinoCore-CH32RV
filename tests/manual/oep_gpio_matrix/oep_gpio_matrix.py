# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Drive and sample every X035 pad that the fixture wires to the P4 (E143 pin map), from both
sides, through the OEP probe: X035 output -> P4 reads; P4 drives -> X035 digitalRead in
INPUT / INPUT_PULLUP / INPUT_PULLDOWN; EXTI RISING / FALLING / CHANGE counts (worklist P3 row 1).

  uv run tests/manual/oep_gpio_matrix/oep_gpio_matrix.py [--pins PA0,PA1] [--result-json out.json]

Excluded on purpose: PB0/PB1 (console USART4), PC10/PC11 (bonded USB pads, never driven by the
core), PC18/PC19 (SWD). PC15 is wired to two P4 pins (15 and 45); 45 is left floating.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "oep_smoke"))
import oep_smoke  # noqa: E402

SKETCH = HERE / "gpio_probe"
UART_RX, UART_TX = 12, 6
# X035 pad -> P4 GPIO (E143, 2026-09-21)
PIN_MAP = {"PA0": 46, "PA1": 47, "PA2": 48, "PA3": 49, "PA4": 53, "PA5": 4, "PA6": 11, "PA7": 5,
           "PB3": 13, "PB11": 9, "PB12": 14, "PC14": 10, "PC15": 15, "PC16": 52, "PC17": 50}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=oep_smoke.DEFAULT_PORT)
    parser.add_argument("--fqbn", default="ch32-riscv-ug:ch32v:CH32X035:pnum=ANY")
    parser.add_argument("--oep-client", default=str(oep_smoke.DEFAULT_CLIENT))
    parser.add_argument("--pins", help="comma separated subset of the pin map")
    parser.add_argument("--settle", type=float, default=0.05, help="seconds between a drive change and the sample (P4 GPIO 9/13/14 release slowly)")
    parser.add_argument("--result-json")
    args = parser.parse_args()
    sys.path.insert(0, args.oep_client)
    from oep_client.v0 import codec
    from oep_client.v0.__main__ import open_client
    from oep_client.v0.flash_image import Target, program_image
    from oep_client.v0.services import FixtureGpio, FixtureUart

    pins = {k: PIN_MAP[k] for k in (args.pins.split(",") if args.pins else PIN_MAP)}
    log = print
    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build("gpio_probe", args.fqbn, 4, pathlib.Path(tmp), log, source=SKETCH)
        image = binary.read_bytes()
    client = open_client(args.port, 3.0)
    target = Target(client)
    outcome = program_image(target, image)
    log(f"programmed {outcome.pages_changed} pages verified={outcome.verified}")
    if not outcome.verified:
        raise SystemExit("program/verify failed")
    uart = FixtureUart(client, client.find(*codec.DEF_FIXTURE_UART[:2]).function)
    gpio = FixtureGpio(client, client.find(*codec.DEF_FIXTURE_GPIO[:2]).function)
    lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX))
    results = {}
    failures = []
    try:
        uart.configure(115200)
        link = oep_smoke.UartLink(uart)
        if not link.wait("gpio_probe READY", 10):
            raise SystemExit(f"no READY: {link.text[:200]!r}")

        def cmd(text, reply, timeout=3):
            link.drain(0.01)
            mark = len(link.text)
            link.send(text + "\n")
            if not link.wait(reply, timeout):
                raise SystemExit(f"no reply to {text!r}: {link.text[mark:][:120]!r}")
            return next(l for l in link.text[mark:].splitlines() if reply in l).strip()

        def dut_read(name):
            return int(cmd(f"READ {name}", "READ v=").split("v=")[1])

        def p4_read(p4):
            return gpio.read(p4)

        for name, p4 in pins.items():
            row = {}
            # 1. X035 drives, P4 samples (P4 input floating)
            gpio.configure(p4, FixtureGpio.INPUT_FLOATING)
            cmd(f"MODE {name} 3", "MODE ok")
            cmd(f"WRITE {name} 0", "WRITE ok"); time.sleep(args.settle); lo = p4_read(p4)
            cmd(f"WRITE {name} 1", "WRITE ok"); time.sleep(args.settle); hi = p4_read(p4)
            row["out"] = (lo, hi)
            # 1b. X035 open-drain: released must be pullable by the P4
            cmd(f"MODE {name} 4", "MODE ok"); cmd(f"WRITE {name} 1", "WRITE ok")
            gpio.configure(p4, FixtureGpio.OUTPUT_LOW); time.sleep(args.settle); od_pulled = dut_read(name)
            gpio.configure(p4, FixtureGpio.INPUT_PULL_UP); time.sleep(args.settle); od_released = dut_read(name)
            cmd(f"WRITE {name} 0", "WRITE ok"); time.sleep(args.settle); od_low = p4_read(p4)
            row["od"] = {"p4_low_reads": od_pulled, "p4_pullup_reads": od_released, "x035_low_p4_reads": od_low}
            gpio.configure(p4, FixtureGpio.INPUT_FLOATING)
            # 2. P4 drives, X035 samples in each input mode
            for m, mname in ((0, "input"), (1, "pullup"), (2, "pulldown")):
                cmd(f"MODE {name} {m}", "MODE ok")
                gpio.configure(p4, FixtureGpio.INPUT_FLOATING); time.sleep(args.settle); idle = dut_read(name)
                gpio.configure(p4, FixtureGpio.OUTPUT_LOW); time.sleep(args.settle); low = dut_read(name)
                gpio.configure(p4, FixtureGpio.OUTPUT_HIGH); time.sleep(args.settle); high = dut_read(name)
                gpio.configure(p4, FixtureGpio.INPUT_FLOATING)
                row[mname] = {"idle": idle, "p4_low": low, "p4_high": high}
            # 3. EXTI counts: the P4 toggles 10 pulses
            cmd(f"MODE {name} 0", "MODE ok")
            gpio.configure(p4, FixtureGpio.OUTPUT_LOW); time.sleep(args.settle)
            exti = {}
            for m, mname, expect in ((0, "rising", 10), (1, "falling", 10), (2, "change", 20)):
                cmd(f"EXTI {name} {m}", "EXTI armed")
                for _ in range(10):
                    gpio.configure(p4, FixtureGpio.OUTPUT_HIGH); gpio.configure(p4, FixtureGpio.OUTPUT_LOW)
                time.sleep(0.01)
                count = int(cmd("COUNT", "COUNT n=").split("n=")[1])
                cmd(f"EXTIOFF {name}", "EXTIOFF ok")
                exti[mname] = (count, expect)
            row["exti"] = exti
            gpio.configure(p4, FixtureGpio.INPUT_FLOATING)
            cmd(f"MODE {name} 0", "MODE ok")
            # judge
            ok = (row["out"] == (0, 1)
                  and row["od"] == {"p4_low_reads": 0, "p4_pullup_reads": 1, "x035_low_p4_reads": 0}
                  and row["input"]["p4_low"] == 0 and row["input"]["p4_high"] == 1
                  and row["pullup"] == {"idle": 1, "p4_low": 0, "p4_high": 1}
                  and row["pulldown"]["p4_low"] == 0 and row["pulldown"]["p4_high"] == 1
                  # P4 GPIO13/14 (SD-card pads) release slowly after driving high; their idle level is not a DUT fact
                  and (row["pulldown"]["idle"] == 0 or p4 in (13, 14))
                  and all(c == e for c, e in exti.values()))
            row["ok"] = ok
            results[name] = row
            if not ok:
                failures.append(name)
            log(f"[{name:4s} <-> P4 GPIO{p4:2d}] {'OK ' if ok else 'BAD'} out={row['out']} od={row['od']} in={row['input']} pu={row['pullup']} pd={row['pulldown']} "
                f"exti r/f/c={exti['rising'][0]}/{exti['falling'][0]}/{exti['change'][0]}")
    finally:
        client.plan_release(lease)
        target.control.reset()
    if args.result_json:
        pathlib.Path(args.result_json).write_text(json.dumps(results, indent=1))
    log(f"pins tested={len(results)} failures={failures}")


if __name__ == "__main__":
    main()
