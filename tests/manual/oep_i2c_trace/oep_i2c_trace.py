# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Trace the X035's own I2C master transactions on the wire (worklist B): the DUT writes to
the probe's I2C target over route 2 while fixture.capture samples SCL/SDA in the same plan,
and the host decodes START / bytes / ACK / STOP next to what Wire reported.

  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py
  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --hz 100000 --hz 10000 --result-json /tmp/trace.json

Fixture (2026-09-22): X035 route 2 PC16 (SCL) / PC17 (SDA) -> P4 GPIO52 / GPIO50; console USART4
PB0/PB1 -> P4 GPIO12/6. Everything goes through the OEP probe (sibling oep-client-python).
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
import oep_smoke  # noqa: E402  (build, UartLink, DEFAULT_PORT, DEFAULT_CLIENT)

SKETCH = HERE / "i2c_probe_write"
SCL, SDA = 52, 50
UART_RX, UART_TX = 12, 6
TARGET_ADDRESS = 0x42


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=oep_smoke.DEFAULT_PORT)
    parser.add_argument("--fqbn", default="ch32-riscv-ug:ch32v:CH32X035:pnum=ANY")
    parser.add_argument("--oep-client", default=str(oep_smoke.DEFAULT_CLIENT))
    parser.add_argument("--hz", type=int, action="append", help="I2C clocks to try (default 100000 and 10000)")
    parser.add_argument("--route", type=int, default=2)
    parser.add_argument("--result-json")
    parser.add_argument("--raw-dir", help="write the raw sample bytes of every capture here")
    args = parser.parse_args()
    clocks = args.hz or [100000, 10000]
    sys.path.insert(0, args.oep_client)
    from oep_client.v0 import codec
    from oep_client.v0.__main__ import open_client
    from oep_client.v0.decode import decode_i2c
    from oep_client.v0.flash_image import Target, program_image
    from oep_client.v0.services import FixtureCapture, FixtureUart, P4I2cTarget

    log = print
    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build("i2c_probe_write", args.fqbn, 4, pathlib.Path(tmp), log, source=SKETCH)
        image = binary.read_bytes()
    client = open_client(args.port, 3.0)
    target = Target(client)
    outcome = program_image(target, image)
    log(f"programmed {outcome.pages_changed} pages verified={outcome.verified} reset_flags=0x{int(outcome.timings['reset_flags']):02x}")
    if not outcome.verified:
        raise SystemExit("program/verify failed")
    uart = FixtureUart(client, client.find(*codec.DEF_FIXTURE_UART[:2]).function)
    i2c = P4I2cTarget(client, client.find(*codec.DEF_P4_I2C_TARGET[:2]).function)
    capture = FixtureCapture(client, client.find(*codec.DEF_FIXTURE_CAPTURE[:2]).function)
    # One plan: console UART + P4 I2C target on SDA50/SCL52 + capture observing the same two lines.
    lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX) + i2c.assignments(sda=SDA, scl=SCL)
                                 + capture.assignments(SCL, SDA))
    results = []
    try:
        uart.configure(115200)
        link = oep_smoke.UartLink(uart)
        if not link.wait("i2c_probe_write READY", 10):
            raise SystemExit(f"no READY: {link.text[:200]!r}")
        case = 0
        link.send(f"BEGIN {args.route}\n"); link.wait("BEGIN route=", 5); time.sleep(0.2)
        # Each clock: target address (first transaction after a settled bus, then a repeat), then nobody.
        for hz in clocks:
            for address, listening in ((TARGET_ADDRESS, True), (TARGET_ADDRESS, True), (TARGET_ADDRESS + 1, False)):
                case += 1
                payload = bytes((0x10 * case + i) & 0xFF for i in range(4))  # distinct per case: stale data shows
                rate, window = 1_000_000, 60_000
                i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_FIXED_RX)
                i2c.arm_rx(len(payload))
                capture.configure(rate, window)
                link.drain(0.1)
                capture.arm()
                link.send(f"WRITE {args.route} {hz} {address:02x} {payload.hex()}\n")
                got_line = link.wait("WRITE rc=", 5)
                st = capture.wait(3.0)
                line = next((l for l in link.text.splitlines()[::-1] if "WRITE rc=" in l), "")
                samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                trace = decode_i2c(samples, scl_bit=0, sda_bit=1)
                time.sleep(0.05)
                pending, data = i2c.read_rx()
                tstat = i2c.status()
                if args.raw_dir and samples:
                    pathlib.Path(args.raw_dir).mkdir(parents=True, exist_ok=True)
                    pathlib.Path(args.raw_dir, f"case{case}_{hz}_{address:02x}.bin").write_bytes(samples)
                periods = sorted(trace.scl_periods)
                period_us = (periods[len(periods) // 2] / rate * 1e6) if periods else 0.0
                row = {"hz": hz, "address": address, "target_listening": listening, "wire_line": line.strip(),
                       "trace": trace.summary(), "scl_period_us": round(period_us, 2),
                       "target_rx": data.hex(), "target_pending": pending, "target_rx_frames": tstat.rx_frames,
                       "target_errors": tstat.errors, "capture_samples": len(samples), "got_line": got_line,
                       "events": [(e.kind, e.sample, e.value, e.ack) for e in trace.events]}
                results.append(row)
                log(f"[{hz} Hz -> 0x{address:02x}{' (target)' if listening else ' (nobody)'}] {line.strip()} | trace: {trace.summary()} "
                    f"| SCL period {period_us:.1f} us | target rx={data.hex() or '-'} pending={pending} frames={tstat.rx_frames} errors={tstat.errors}")
                log(f"    events: {[(e.kind, e.sample, hex(e.value), e.ack) for e in trace.events]}")
    finally:
        client.plan_release(lease)
        target.control.reset()
    if args.result_json:
        pathlib.Path(args.result_json).write_text(json.dumps({"port": args.port, "results": results}, indent=1))
        log(f"wrote {args.result_json}")


if __name__ == "__main__":
    main()
