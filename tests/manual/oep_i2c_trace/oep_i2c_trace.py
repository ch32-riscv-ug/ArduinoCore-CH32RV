# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Trace the X035's own I2C master transactions on the wire (worklist B): the DUT writes to
the probe's I2C target over route 2 while fixture.capture samples SCL/SDA in the same plan,
and the host decodes START / bytes / ACK / STOP next to what Wire reported.

  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py
  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --hz 100000 --hz 10000 --result-json /tmp/trace.json
  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --rw        # read / repeated START / 400 kHz
  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --stretch   # target stretches SCL 0.1..30 ms
  uv run tests/manual/oep_i2c_trace/oep_i2c_trace.py --stuck     # lines held low, target left mid-byte, bus clear

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
import targets  # noqa: E402

SKETCH = HERE / "i2c_probe_write"
SCL, SDA = 52, 50
UART_RX, UART_TX = 12, 6
TARGET_ADDRESS = 0x42


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    targets.add_target_argument(parser)
    parser.add_argument("--port", help="probe serial port (default: the target profile's)")
    parser.add_argument("--fqbn", help="DUT board (default: the target profile's)")
    parser.add_argument("--oep-client", default=str(oep_smoke.DEFAULT_CLIENT))
    parser.add_argument("--hz", type=int, action="append", help="I2C clocks to try (default 100000 and 10000)")
    parser.add_argument("--route", type=int, help="DUT Wire route (default: the target profile's)")
    parser.add_argument("--probe-scl", type=int, help="probe GPIO on the DUT's SCL for this route (default: the profile's)")
    parser.add_argument("--probe-sda", type=int, help="probe GPIO on the DUT's SDA for this route (default: the profile's)")
    parser.add_argument("--result-json")
    parser.add_argument("--raw-dir", help="write the raw sample bytes of every capture here")
    parser.add_argument("--bitbang", action="store_true", help="sweep a bit-banged master (SDA hold x drive) instead of Wire")
    parser.add_argument("--hold-sweep", action="store_true", help="fast bit-bang: sweep the SDA hold in ns and report the ACK threshold")
    parser.add_argument("--rw", action="store_true", help="read from the target's preloaded slots, repeated START (write then read), 400 kHz write")
    parser.add_argument("--stretch", action="store_true", help="target stretches SCL (p4.i2c-target set_stretch) for 0.1..30 ms; Wire must wait, then time out gracefully")
    parser.add_argument("--stuck", action="store_true", help="SDA/SCL held low by the P4, then a target left driving SDA low mid-byte; Wire error codes, timing, recovery")
    args = parser.parse_args()
    profile = targets.TARGETS[args.target]
    global SCL, SDA, UART_RX, UART_TX
    SCL, SDA, UART_RX, UART_TX = profile["i2c"]["scl"], profile["i2c"]["sda"], profile["uart_rx"], profile["uart_tx"]
    if args.probe_scl is not None: SCL = args.probe_scl
    if args.probe_sda is not None: SDA = args.probe_sda
    if args.route is None: args.route = profile["i2c"]["route"]
    has_capture = profile["capture"]
    clocks = args.hz or [100000, 10000]
    sys.path.insert(0, args.oep_client)
    from oep_client.v0 import codec
    from oep_client.v0.__main__ import open_client
    from oep_client.v0.decode import decode_i2c
    from oep_client.v0.flash_image import Target, program_image
    from oep_client.v0.services import FixtureCapture, FixtureGpio, FixtureUart, P4I2cTarget

    log = print
    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build("i2c_probe_write", args.fqbn or profile["fqbn"], profile["serial_index"], pathlib.Path(tmp), log,
                                 source=SKETCH, defines=targets.build_defines(profile))
        image = binary.read_bytes()
    client = open_client(args.port or profile["port"], 3.0)
    target = Target(client)
    outcome = program_image(target, image)
    log(f"programmed {outcome.pages_changed} pages verified={outcome.verified} reset_flags=0x{int(outcome.timings['reset_flags']):02x}")
    if not outcome.verified:
        raise SystemExit("program/verify failed")
    uart = FixtureUart(client, client.find(*codec.DEF_FIXTURE_UART[:2]).function)
    i2c = P4I2cTarget(client, client.find(*codec.DEF_P4_I2C_TARGET[:2]).function)
    capture = FixtureCapture(client, client.find(*codec.DEF_FIXTURE_CAPTURE[:2]).function) if has_capture else None
    # One plan: console UART + probe I2C target + (on the P4) capture observing the same two lines.
    lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX) + i2c.assignments(sda=SDA, scl=SCL)
                                 + (capture.assignments(SCL, SDA) if has_capture else []))
    results = []
    try:
        uart.configure(115200)
        link = oep_smoke.UartLink(uart)
        if not link.wait("i2c_probe_write READY", 10):
            raise SystemExit(f"no READY: {link.text[:200]!r}")
        if args.rw:
            from oep_client.v0.decode import I2cTrace
            link.send(f"BEGIN {args.route}\n"); link.wait("BEGIN route=", 5); time.sleep(0.2)
            def trace_cmd(command, reply, hz):
                rate = min(5_000_000 if hz >= 400_000 else 1_000_000, profile.get("capture_max_hz", 5_000_000))
                if has_capture: capture.configure(rate, 65000)
                link.drain(0.05)
                if has_capture: capture.arm()
                link.send(command + "\n")
                if not link.wait(reply, 5): raise SystemExit(f"no reply to {command!r}")
                samples = b""
                if has_capture:
                    st = capture.wait(3.0); samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                line = next((l for l in link.text.splitlines()[::-1] if reply in l), "").strip()
                return line, decode_i2c(samples, scl_bit=0, sda_bit=1)
            # 1. READ: the P4 target answers from preloaded 4-byte slots
            i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_PRELOADED_TX)
            slots = [bytes.fromhex("a1b2c3d4"), bytes.fromhex("11223344")]
            for s_ in slots: i2c.preload_tx(s_)
            for hz, expect in ((100000, slots[0]), (100000, slots[1])):
                line, trace = trace_cmd(f"READ {args.route} {hz} {TARGET_ADDRESS:02x} 4", "READ got=", hz)
                data_hex = line.split("data=")[1].split()[0] if "data=" in line else ""
                got = bytes.fromhex(data_hex) if data_hex else b""
                log(f"[read {hz} Hz] {line} | wire {trace.summary()} | expected {expect.hex()} match={got == expect}")
                results.append({"read": True, "hz": hz, "line": line, "trace": trace.summary(), "match": got == expect})
            # 2. repeated START: write 2 bytes without STOP, then read 4 (the target's next slot)
            i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_PRELOADED_TX); i2c.preload_tx(bytes.fromhex("55667788"))
            line, trace = trace_cmd(f"WRREAD {args.route} 100000 {TARGET_ADDRESS:02x} 0102", "WRREAD rc=", 100000)
            log(f"[write-then-read, repeated START] {line} | wire {trace.summary()}")
            results.append({"wrread": True, "line": line, "trace": trace.summary()})
            # 3. 400 kHz write into fixed-rx
            i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_FIXED_RX); i2c.arm_rx(4)
            line, trace = trace_cmd(f"WRITE {args.route} 400000 {TARGET_ADDRESS:02x} 0a0b0c0d", "WRITE rc=", 400000)
            time.sleep(0.05); pending, rx = i2c.read_rx()
            period = sorted(trace.scl_periods)[len(trace.scl_periods) // 2] / 5_000_000 * 1e6 if trace.scl_periods else 0
            log(f"[write 400 kHz] {line} | wire {trace.summary()} | SCL period {period:.2f} us | target rx={rx.hex()}")
            results.append({"write400": True, "line": line, "trace": trace.summary(), "rx": rx.hex(), "scl_us": period})
            clocks = []
        def scl_low_max_us(samples, rate):   # longest SCL-low run inside the capture, in microseconds
            best = run = 0
            for b in samples:
                if b & 1: best = max(best, run); run = 0
                else: run += 1
            return max(best, run) / rate * 1e6

        def wire_write(hz, data_hex, rate=1_000_000):
            capture.configure(rate, 65000); link.drain(0.05); capture.arm()
            link.send(f"WRITE {args.route} {hz} {TARGET_ADDRESS:02x} {data_hex}\n")
            if not link.wait("WRITE rc=", 5): raise SystemExit("no reply to WRITE")
            st = capture.wait(3.0); samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
            line = next((l for l in link.text.splitlines()[::-1] if "WRITE rc=" in l), "").strip()
            rc = int(line.split("rc=")[1].split()[0]); t_us = int(line.split("t_us=")[1].split()[0])
            return rc, t_us, line, samples, decode_i2c(samples, scl_bit=0, sda_bit=1)

        def wire_read(hz, count, rate=1_000_000):
            capture.configure(rate, 65000); link.drain(0.05); capture.arm()
            link.send(f"READ {args.route} {hz} {TARGET_ADDRESS:02x} {count}\n")
            if not link.wait("READ got=", 5): raise SystemExit("no reply to READ")
            st = capture.wait(3.0); samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
            line = next((l for l in link.text.splitlines()[::-1] if "READ got=" in l), "").strip()
            got = int(line.split("got=")[1].split()[0]); data = line.split("data=")[1].split()[0]; t_us = int(line.split("t_us=")[1].split()[0])
            return got, data.lower(), t_us, line, samples, decode_i2c(samples, scl_bit=0, sda_bit=1)

        if args.stretch:
            # The target holds every hardware stretch for stretch_us. The ESP32 slave stretches at address match only
            # for a master READ (so it can prepare TX data), so the test reads 4 preloaded bytes. Wire's default timeout
            # is 25 ms (CH32_WIRE_TIMEOUT_US): below it the read must complete with the slot's bytes; above it Wire must
            # give up within the timeout, and the next transaction (stretch off) must work again.
            link.send(f"BEGIN {args.route}\n"); link.wait("BEGIN route=", 5); time.sleep(0.2)
            slot = "a1b2c3d4"
            for stretch_us in (0, 100, 1000, 5000, 20000, 30000):
                i2c.set_stretch(stretch_us); i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_PRELOADED_TX); i2c.preload_tx(bytes.fromhex(slot))
                hw0 = i2c.read_hw()
                got, data, t_us, line, samples, trace = wire_read(100000, 4)
                hw1 = i2c.read_hw()
                low_us = scl_low_max_us(samples, 1_000_000)
                expect_ok = stretch_us < 25000
                ok = (got == 4 and data == slot) if expect_ok else (got == 0 and t_us < 40000)
                log(f"[stretch {stretch_us:6d} us, READ 4] got={got} data={data or '-'} t={t_us/1000:.2f} ms max SCL low {low_us/1000:.2f} ms | wire {trace.summary()} -> {'OK' if ok else 'BAD'}")
                log(f"    hw: scl_stretch_conf 0x{hw0.scl_stretch_conf:08x} -> 0x{hw1.scl_stretch_conf:08x}, sr 0x{hw1.sr:08x}, int_raw 0x{hw1.int_raw:08x}")
                results.append({"stretch_us": stretch_us, "got": got, "data": data, "t_us": t_us, "scl_low_max_us": low_us, "trace": trace.summary(), "ok": ok})
            i2c.set_stretch(0); i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_PRELOADED_TX); i2c.preload_tx(bytes.fromhex(slot))
            got, data, t_us, line, samples, trace = wire_read(100000, 4)
            log(f"[after stretch timeout, stretch off, READ 4] got={got} data={data or '-'} t={t_us/1000:.2f} ms | wire {trace.summary()} -> {'OK' if got == 4 and data == slot else 'BAD'}")
            results.append({"after_stretch": True, "got": got, "data": data, "t_us": t_us})
            clocks = []
        if args.stuck:
            gpio = FixtureGpio(client, client.find(*codec.DEF_FIXTURE_GPIO[:2]).function)
            link.send(f"BEGIN {args.route}\n"); link.wait("BEGIN route=", 5); time.sleep(0.2)
            # Part 1: a line held low by "something else" (P4 GPIO). The target leaves the plan so the pins are free.
            client.plan_release(lease)
            lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX) + capture.assignments(SCL, SDA))
            uart.configure(115200); link.send("\n"); link.drain(0.3)   # a new lease starts unconfigured
            for name, pin in (("SDA", SDA), ("SCL", SCL)):
                gpio.configure(pin, FixtureGpio.OUTPUT_LOW); time.sleep(0.01)
                rc, t_us, line, samples, trace = wire_write(100000, "0102")
                log(f"[{name} held low by P4] rc={rc} t={t_us/1000:.2f} ms | {line} | wire {trace.summary()}")
                results.append({"held": name, "rc": rc, "t_us": t_us, "line": line})
                rc2, t2, line2, _, _ = wire_write(100000, "0102")
                log(f"[{name} still low, 2nd try] rc={rc2} t={t2/1000:.2f} ms")
                gpio.configure(pin, FixtureGpio.INPUT_FLOATING); time.sleep(0.01)
                rc3, t3, line3, _, trace3 = wire_write(100000, "0102")
                log(f"[{name} released, nobody listening] rc={rc3} t={t3/1000:.2f} ms | wire {trace3.summary()} -> {'OK' if rc3 == 2 else 'BAD'} (expect 2 = address NACK)")
                results.append({"released": name, "rc": rc3, "t_us": t3})
            # Part 2: a real target left driving SDA low in the middle of a byte.
            client.plan_release(lease)
            lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX) + i2c.assignments(sda=SDA, scl=SCL) + capture.assignments(SCL, SDA))
            uart.configure(115200); link.send("\n"); link.drain(0.3)
            i2c.set_stretch(0); i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_PRELOADED_TX); i2c.preload_tx(bytes(4))
            capture.configure(1_000_000, 65000); link.drain(0.05); capture.arm()
            link.send(f"STUCK {TARGET_ADDRESS:02x}\n"); link.wait("STUCK ack=", 5)
            st = capture.wait(3.0); samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
            stuck_line = next((l for l in link.text.splitlines()[::-1] if "STUCK ack=" in l), "").strip()
            log(f"[target left mid-byte] {stuck_line} | wire {decode_i2c(samples, 0, 1).summary()}")
            link.send(f"BEGIN {args.route}\n"); link.wait("BEGIN route=", 5); time.sleep(0.05)
            for attempt in (1, 2):
                rc, t_us, line, samples, trace = wire_write(100000, "0102")
                log(f"[Wire.begin + write, SDA held by target, try {attempt}] rc={rc} t={t_us/1000:.2f} ms | wire {trace.summary()}")
                results.append({"stuck_write": attempt, "rc": rc, "t_us": t_us})
            link.drain(0.05); link.send("BUSCLR\n"); link.wait("BUSCLR pulses=", 5)
            clr_line = next((l for l in link.text.splitlines()[::-1] if "BUSCLR pulses=" in l), "").strip()
            log(f"[bus clear from the sketch] {clr_line}")
            link.send(f"BEGIN {args.route}\n"); link.wait("BEGIN route=", 5); time.sleep(0.05)
            i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_FIXED_RX); i2c.arm_rx(2)
            rc, t_us, line, samples, trace = wire_write(100000, "0102"); time.sleep(0.05); pending, rx = i2c.read_rx()
            log(f"[after bus clear] rc={rc} t={t_us/1000:.2f} ms | wire {trace.summary()} | target rx={rx.hex() or '-'} -> {'OK' if rc == 0 and rx.hex() == '0102' else 'BAD'}")
            results.append({"after_busclr": True, "rc": rc, "stuck": stuck_line, "busclr": clr_line, "rx": rx.hex()})
            clocks = []
        if args.hold_sweep:
            payload = bytes.fromhex("10111213")
            for hold in (0, 200, 400, 600, 800, 1000, 1500, 2000, 3000):
                i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_FIXED_RX); i2c.arm_rx(len(payload))
                capture.configure(5_000_000, 65000); link.drain(0.1); capture.arm()
                link.send(f"BBF {hold} {TARGET_ADDRESS:02x} {payload.hex()}\n"); link.wait("BBF hold_ns=", 5)
                st = capture.wait(3.0)
                samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                trace = decode_i2c(samples, scl_bit=0, sda_bit=1)
                line = next((l for l in link.text.splitlines()[::-1] if "BBF hold_ns=" in l), "").strip()
                prev = samples[0] if samples else 0; falls = []; changes = []
                for i in range(1, len(samples)):
                    if (prev & 1) and not (samples[i] & 1): falls.append(i)
                    if (prev ^ samples[i]) & 2: changes.append((i, (samples[i] >> 1) & 1))
                    prev = samples[i]
                falls_set = falls
                rel0 = sorted((s_ - f) * 200 for s_, lvl in changes for f in [min(falls_set, key=lambda x: abs(x - s_))] if lvl == 0 and 0 <= s_ - f < 25) if falls_set else []
                rel1 = sorted((s_ - f) * 200 for s_, lvl in changes for f in [min(falls_set, key=lambda x: abs(x - s_))] if lvl == 1 and 0 <= s_ - f < 40) if falls_set else []
                scl = sorted(trace.scl_periods); period = scl[len(scl) // 2] * 200 if scl else 0
                log(f"[hold {hold:4d} ns] {line} | wire {trace.summary()} | measured SDA fall after SCL fall {rel0[:3]} ns, SDA high after SCL fall {rel1[:3]} ns, SCL period {period} ns")
                results.append({"hold_sweep": True, "hold_ns": hold, "line": line, "trace": trace.summary(), "fall_ns": rel0[:3], "rise_ns": rel1[:3]})
            clocks = []
        if args.bitbang:
            payload = bytes.fromhex("10111213")
            for drive in (0, 1):
                for hold in (0, 1, 2, 4):
                    i2c.configure(TARGET_ADDRESS, P4I2cTarget.MODE_FIXED_RX); i2c.arm_rx(len(payload))
                    capture.configure(5_000_000, 65000); link.drain(0.1); capture.arm()
                    link.send(f"BB {hold} {drive} {TARGET_ADDRESS:02x} {payload.hex()}\n")
                    link.wait("BB hold_us=", 5)
                    st = capture.wait(3.0)
                    samples = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                    trace = decode_i2c(samples, scl_bit=0, sda_bit=1)
                    line = next((l for l in link.text.splitlines()[::-1] if "BB hold_us=" in l), "").strip()
                    # SDA change after the nearest SCL fall, and SDA 0->1 rise delay, in ns
                    prev = samples[0] if samples else 0; falls = []; sda_changes = []
                    for i in range(1, len(samples)):
                        if (prev & 1) and not (samples[i] & 1): falls.append(i)
                        if (prev ^ samples[i]) & 2: sda_changes.append((i, (samples[i] >> 1) & 1))
                        prev = samples[i]
                    rel = []
                    for s_, lvl in sda_changes:
                        near = min(falls, key=lambda f: abs(f - s_)) if falls else None
                        if near is not None and 0 <= s_ - near < 15000: rel.append((round((s_ - near) * 200), lvl))
                    zeros = sorted(d for d, l in rel if l == 0)[:3]; ones = sorted(d for d, l in rel if l == 1)[:3]
                    log(f"[bitbang hold={hold} us drive={'pp' if drive else 'od'}] {line} | wire: {trace.summary()} | "
                        f"SDA fall after SCL fall {zeros} ns, SDA rise after SCL fall {ones} ns")
                    results.append({"bitbang": True, "hold_us": hold, "drive": drive, "line": line, "trace": trace.summary()})
            clocks = []
        case = 0
        if clocks:
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
