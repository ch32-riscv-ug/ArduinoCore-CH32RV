# /// script
# requires-python = ">=3.13"
# dependencies = ["pyserial>=3.5", "wireskein>=0.0.1", "oep-client-python>=0.0.4"]
# ///
"""X035 USART2 against the probe's second fixture.uart (worklist P3 row 2): DUT -> P4 and P4 -> DUT
(echo) binary payloads at several bauds, a long continuous transfer, receive overflow behaviour of
the 64-byte ring, and resumption after a debug reset.

  uv run tests/manual/oep_uart_trace/oep_uart_trace.py [--bauds 9600,115200,460800]
  uv run tests/manual/oep_uart_trace/oep_uart_trace.py --sweep --target x035 --run-dir /tmp/uart-sweep

--sweep changes the profile's UART at begin() only (one build): the standard bauds, for each small BRR the baud its
rounding treats worst, bauds above the range and other formats (8E1 8O1 8N2 7E1 7O2). Each is checked both ways
against the probe's fixture.uart and measured on the wire: the core sets BRR = round(F_CPU / baud), so the real
baud is F_CPU / BRR, and some bauds land far from the ask (F_CPU / 16.49: +3.1 %) or below BRR 16, out of range.

Wiring (E143): X035 PA2 (USART2 TX) -> P4 GPIO48, PA3 (RX) <- P4 GPIO49. The console is the probe's
target.console (tests/sketches/testcmd.h), so the one UART in play is the one under test.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "oep_smoke"))
import oep_smoke  # noqa: E402
import targets  # noqa: E402
import trace_kit  # noqa: E402

SKETCH = HERE / "uart_probe"
UART2_PADS = ("PA2", "PA3")   # probe RX = DUT TX (PA2), probe TX = DUT RX (PA3)


def lcg_bytes(n: int, seed: int) -> bytes:
    x = seed & 0xFFFFFFFF
    out = bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


SWEEP_BAUDS = "9600,19200,38400,57600,74880,115200,230400,250000,460800,500000,921600,1000000,1500000,2000000,3000000"
SWEEP_FORMATS = ("8E1", "8O1", "8N2", "7E1", "7O2")
WORST_BRR = (16, 17, 18, 20, 24, 32, 48, 64)   # BRR values whose rounding is tried at its worst
# begin() on a running port: up and down in speed, formats changing with it
SWITCH_SEQUENCE = ((115200, "8N1"), (1000000, "8N1"), (9600, "8E1"), (2000000, "8N1"), (57600, "8O1"),
                   (460800, "7E1"), (115200, "8N2"), (115200, "8N1"))
PENDING_PAIRS = ((115200, 1000000), (1000000, 115200), (9600, 115200))   # begin(b2) with bytes still leaving at b1


def expect_brr(f_cpu: int, baud: int) -> tuple[int, float, float]:
    """The core's BRR (HardwareSerial.cpp: round(F_CPU / baud)), the baud it gives and its error against the ask."""
    brr = (f_cpu + baud // 2) // baud if baud else 0
    real = f_cpu / brr if brr else 0.0
    return brr, real, (real / baud - 1) if brr else float("nan")


def sweep_cases(f_cpu: int, bauds: list[int]) -> list[tuple[int, str, str]]:
    """(baud, format, why): the standard bauds in 8N1; for each BRR in WORST_BRR the baud F_CPU / (BRR + 0.49), the
    one that rounds down to it with the largest error (+3.1 % at 16); above the range F_CPU / 12 and / 8; the other
    formats at 115200 and at the fastest baud in range. Out of range begin() must leave the port closed."""
    cases = [(b, "8N1", "standard") for b in bauds]
    cases += [(int(f_cpu / (n + 0.49)), "8N1", f"worst rounding at BRR {n}") for n in WORST_BRR]
    cases += [(f_cpu // 12, "8N1", "above the range (BRR 12)"), (f_cpu // 8, "8N1", "above the range (BRR 8)"),
              (f_cpu // 65535 + 1, "8N1", "the slowest in range"), (f_cpu // 70000, "8N1", "below the range (BRR > 0xFFFF)"),
              (0, "8N1", "zero")]
    for fmt in SWEEP_FORMATS:
        cases += [(115200, fmt, "format"), (f_cpu // 16, fmt, "format at BRR 16")]
    return cases


def sweep(args, profile: dict, log) -> int:
    from oep_client.fixture import FixtureUart
    from oep_client.host import Unsupported
    usart, _route = profile["uart"]
    tx_pad = targets.pin_name(profile, profile["uart_rx"])     # the DUT's TX lands on the probe's RX
    run_ = trace_kit.Run(args, profile, test="oep_uart_trace --sweep")
    rec = run_.rec
    cap_max = profile.get("capture_max_hz", 0) if profile["capture"] else 0
    sess = trace_kit.Session(args.port, profile, "uart_probe", log, source=SKETCH, fqbn=args.fqbn,
                             defines=targets.build_defines(profile) + [f"-DUUT_SERIAL=Serial{usart}"], run=run_)
    uart = trace_kit.Uart(sess)
    cap = trace_kit.Capture(sess) if cap_max else None
    sess.plan(uart.assignments(rx=profile["uart_rx"], tx=profile["uart_tx"])
              + (cap.assignments(profile["uart_rx"], profile["uart_tx"]) if cap else []))
    link = sess.link
    rows = []
    try:
        if not oep_smoke.sync(link, "uart_probe READY"):
            raise SystemExit(f"no READY/PONG: {link.text[:200]!r}")

        def cmd(text, reply, timeout=10):
            link.drain(0.01); link.send(text + "\n")
            if not link.wait(reply, timeout):
                return None
            return next((l for l in link.text.splitlines()[::-1] if reply in l), "").strip()

        def collect(n, timeout):
            buf = bytearray(); t0 = time.perf_counter()
            while len(buf) < n and time.perf_counter() - t0 < timeout:
                chunk = uart.read(1024)
                if chunk: buf += chunk
            return bytes(buf)

        f_cpu = int(cmd("CLOCK", "CLOCK f_cpu=").split("=")[1])
        bauds = [int(b) for b in (args.bauds or SWEEP_BAUDS).split(",")]

        def case(n, baud, fmt, why, close=True):
            brr, real, err = expect_brr(f_cpu, baud)
            bits, parity, stop = int(fmt[0]), {"N": "none", "E": "even", "O": "odd"}[fmt[1]], int(fmt[2])
            mask = (1 << bits) - 1
            row = {"baud": baud, "format": fmt, "why": why, "brr": brr, "expected_error": err, "tx_ok": None, "rx_ok": None}
            with rec.section(2, f"{baud} {fmt}"):
                rec.note(f"{why}: BRR {brr}, expected {real:.1f} baud ({err * 100:+.3f} %)" + ("" if brr >= 16 else ", below BRR 16"))
                opened = cmd(f"OPEN {baud} {fmt}", "OPEN ")
                row["closed"] = opened is not None and "closed" in opened
                row["refusal_ok"] = row["closed"] == (not 16 <= brr <= 0xFFFF)   # refused exactly when out of range
                if row["closed"]:
                    rec.note("begin() left the port closed")
                    rows.append(row)
                    log(f"[{baud:>7} {fmt}] BRR {brr:>5} ({why}) | begin() left the port closed -> "
                        f"{'ok (out of range)' if row['refusal_ok'] else 'BAD (in range)'}")
                    return
                if not row["refusal_ok"]:   # out of range yet open: nothing below would mean anything
                    rows.append(row)
                    log(f"[{baud:>7} {fmt}] BRR {brr:>5} ({why}) | BAD: begin() opened the port out of range")
                    cmd("CLOSE", "CLOSE ok")
                    return
                seed = (n * 7919 + baud) & 0xFFFF
                try:
                    row["probe_baud"] = uart.configure(baud, FixtureUart.format_byte(bits, fmt[1], stop))
                except Unsupported:   # beyond the probe's UART: the wire still measures the DUT's TX
                    row["probe_baud"] = None
                    rec.note(f"the probe's UART does not run {baud} {fmt}: no data comparison")
                if row["probe_baud"] is not None:
                    uart.read(4096); time.sleep(0.02)
                    # DUT -> probe: 256 bytes compared byte for byte (7-bit formats: the low 7 bits)
                    with rec.section(3, "tx"):
                        cmd(f"SEND 256 {seed}", "SEND done", timeout=5)
                        got = collect(256, 0.5 + 2560 / baud)
                    want = bytes(b & mask for b in lcg_bytes(256, seed))
                    got = bytes(b & mask for b in got)
                    row["tx_ok"] = got == want
                    row["tx_bad"] = sum(a != b for a, b in zip(got, want)) + abs(len(want) - len(got))
                    # probe -> DUT -> probe: 64 bytes echoed in 16-byte steps (the DUT ring is 64 bytes)
                    with rec.section(3, "rx"):
                        payload = bytes(b & mask for b in lcg_bytes(64, seed ^ 0x55))
                        link.drain(0.01); link.send("ECHO 64 300\n"); time.sleep(0.03)
                        echoed = bytearray()
                        for off in range(0, 64, 16):
                            uart.write(payload[off:off + 16])
                            deadline = time.perf_counter() + 0.3 + 160 / baud
                            while len(echoed) < off + 16 and time.perf_counter() < deadline:
                                chunk = uart.read(256)
                                if chunk: echoed += chunk
                        link.wait("ECHO n=", 3)
                    row["rx_ok"] = bytes(b & mask for b in echoed) == payload
                # the wire: the baud measured over a window inside a burst (8 samples a bit at least). Below BRR 16
                # the USART is out of range and nothing is promised: the wire is only measured (uart(pin, None)).
                rate = min(cap_max, max(1_000_000, 20 * baud))
                if cap and rate >= 8 * baud:
                    cap.configure(rate, int(min(1_000_000, rate * max(0.02, 400 / baud))))   # ~40 frames
                # judged on what the probe chose: 8 samples a bit, and a window of two bursts (16 bytes + the gap, twice)
                if cap and rate >= 8 * baud and cap.rate < 8 * baud:
                    rec.note(f"the capture runs at {cap.rate:.0f} Hz: under 8 samples a bit, the wire is not taken")
                elif cap and rate >= 8 * baud and cap.config.samples / cap.rate < 360 / baud:
                    rec.note(f"the capture holds {cap.config.samples / cap.rate * 1000:.0f} ms: under two bursts at this baud, "
                             "the wire is not taken")
                elif cap and rate >= 8 * baud:
                    fmt_kw = {"bits": bits, "parity": parity, "stop": stop}
                    want = (run_.uart(tx_pad, real, None, tol_baud=0.015, max_errors=0, **fmt_kw) if brr >= 16
                            else run_.uart(tx_pad, None, **fmt_kw))
                    with rec.section(3, "wire", expect=[want]):
                        window = cap.config.samples / cap.rate
                        link.drain(0.01); cap.arm()
                        link.send(f"BURST {int(window * 1000) + 150} {seed}\n")
                        st = cap.wait(3.0 + window)
                        cap.read_all(st.samples) if st.flags & trace_kit.Capture.COMPLETE else b""
                        link.wait("BURST blocks=", 3)
                    while row["probe_baud"] is not None and uart.read(4096):   # the burst's bytes: only drained
                        pass
                if close:
                    cmd("CLOSE", "CLOSE ok")
            rows.append(row)
            verdict = {True: "ok", False: "BAD", None: "-"}
            log(f"[{baud:>7} {fmt}] BRR {brr:>5} expected {err * 100:+7.3f} %{' (BRR < 16)' if brr < 16 else ''} ({why})"
                f" | probe {row['probe_baud'] or 'cannot'}"
                f" | DUT->probe {verdict[row['tx_ok']]}{' (' + str(row['tx_bad']) + ' bytes)' if row['tx_ok'] is False else ''}"
                f" | echo {verdict[row['rx_ok']]}"
                f"{'' if row['refusal_ok'] else ' | BAD: begin() kept an out-of-range baud open'}")

        rec.heading(1, f"uart f_cpu={f_cpu}")
        cases = sweep_cases(f_cpu, bauds)
        for n, (baud, fmt, why) in enumerate(cases):
            case(n, baud, fmt, why)
        rec.heading(1)
        # begin() again without end(): speed and format changed on a running port, back and forth
        rec.heading(1, "uart switch without end")
        for k, (baud, fmt) in enumerate(SWITCH_SEQUENCE):
            case(len(cases) + k, baud, fmt, "switched without end()", close=False)
        cmd("CLOSE", "CLOSE ok")
        rec.heading(1)
        # begin() while bytes are still going out: the next block must come out whole at the new baud
        rec.heading(1, "uart begin while sending")
        for k, (b1, b2) in enumerate(PENDING_PAIRS):
            with rec.section(2, f"{b1} to {b2}"):
                seed = 0x4000 + k
                cmd(f"OPEN {b1}", "OPEN ok"); uart.configure(b2); uart.read(4096); time.sleep(0.02)
                if cap:
                    cap.configure(min(cap_max, max(1_000_000, 20 * max(b1, b2))), 200_000)
                    cap.arm()
                reply = cmd(f"SWITCH {b2} {seed}", "SWITCH done", timeout=5)
                if cap:
                    st = cap.wait(3.0)
                    cap.read_all(st.samples) if st.flags & trace_kit.Capture.COMPLETE else b""
                got = collect(64, 0.5)
                want = lcg_bytes(16, seed + 1)
                ok = got.endswith(want)
                rows.append({"baud": b2, "format": "8N1", "why": f"begin({b2}) while sending at {b1}", "brr": 0,
                             "expected_error": 0.0, "tx_ok": ok, "rx_ok": None})
                rec.note(f"received {len(got)} bytes at {b2}: {got.hex()}; the block after begin() {'whole' if ok else 'NOT whole'}")
                log(f"[begin({b2}) while sending at {b1}] {len(got)} bytes at {b2}, the block after begin() "
                    f"{'came out whole' if ok else 'BAD'} | {reply}")
                cmd("CLOSE", "CLOSE ok")
        rec.heading(1)
    finally:
        sess.close()
    bad = [r for r in rows if r["tx_ok"] is False or r["rx_ok"] is False or r.get("refusal_ok") is False]
    log(f"cases {len(rows)}, data wrong in {len(bad)}: "
        + ", ".join(f"{r['baud']} {r['format']} ({r['expected_error'] * 100:+.2f} %, BRR {r['brr']})" for r in bad))
    code = run_.verify(log)
    return code or (1 if bad else 0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    targets.add_target_argument(parser)
    parser.add_argument("--port", help="probe serial port (default: the target profile's)")
    parser.add_argument("--fqbn", help="DUT board (default: the target profile's)")
    parser.add_argument("--bauds", help=f"default 9600,115200,460800; with --sweep {SWEEP_BAUDS}")
    parser.add_argument("--sweep", action="store_true", help="F_CPU x baud over the profile's UART (see above)")
    trace_kit.add_run_arguments(parser)
    args = parser.parse_args()
    if args.sweep:
        sys.exit(sweep(args, targets.TARGETS[args.target], print))
    if args.target != "x035":
        raise SystemExit("without --sweep this test runs on the x035 (USART2 on PA2/PA3)")
    args.bauds = args.bauds or "9600,115200,460800"

    log = print
    profile = targets.TARGETS["x035"]
    session = trace_kit.Session(args.port, profile, "uart_probe", log, source=SKETCH, fqbn=args.fqbn or profile["fqbn"])
    uart2 = trace_kit.Uart(session)
    session.plan(uart2.assignments(rx=targets.probe_gpio(profile, UART2_PADS[0]), tx=targets.probe_gpio(profile, UART2_PADS[1])))
    failures = []
    try:
        link = session.link
        if not oep_smoke.sync(link, "uart_probe READY"):
            raise SystemExit(f"no READY/PONG: {link.text[:200]!r}")

        def cmd(text, reply, timeout=10):
            link.drain(0.01); mark = len(link.text); link.send(text + "\n")
            if not link.wait(reply, timeout):
                raise SystemExit(f"no reply to {text!r}: {link.text[mark:][:160]!r}")
            return next(l for l in link.text[mark:].splitlines() if reply in l).strip()

        def drain2(expect_n, timeout):
            buf = bytearray(); t0 = time.perf_counter()
            while len(buf) < expect_n and time.perf_counter() - t0 < timeout:
                chunk = uart2.read(1024)
                if chunk: buf += chunk
            return bytes(buf)

        for baud in (int(b) for b in args.bauds.split(",")):
            cmd(f"OPEN {baud}", "OPEN ok"); uart2.configure(baud); uart2.read(4096); time.sleep(0.05)
            # DUT -> P4, 4096 bytes
            n, seed = 4096, 0x1234 + baud
            t0 = time.perf_counter(); cmd(f"SEND {n} {seed}", "SEND done", timeout=30)
            got = drain2(n, 5.0); dt = time.perf_counter() - t0
            ok = got == lcg_bytes(n, seed)
            log(f"[{baud:>6} baud] DUT->P4 {n} B: {len(got)} B received match={ok} ({n * 10 / dt / 1000:.1f} kbit/s incl. command)")
            if not ok: failures.append(f"{baud} dut->p4")
            # P4 -> DUT -> P4 echo, 2048 bytes in 64-byte chunks (the DUT ring is 64 bytes: pace by reading the echo)
            payload = lcg_bytes(2048, seed ^ 0x55)
            link.drain(0.01); link.send(f"ECHO {len(payload)} 2000\n"); time.sleep(0.05)
            echoed = bytearray(); t0 = time.perf_counter()
            for off in range(0, len(payload), 32):
                uart2.write(payload[off:off + 32])
                deadline = time.perf_counter() + 2.0
                while len(echoed) < off + 32 and time.perf_counter() < deadline:
                    chunk = uart2.read(256)
                    if chunk: echoed += chunk
            reply = next((l for l in link.text.splitlines()[::-1] if "ECHO n=" in l), "") if link.wait("ECHO n=", 3) else "(no ECHO reply)"
            ok = bytes(echoed) == payload
            log(f"[{baud:>6} baud] P4->DUT->P4 echo {len(payload)} B: {len(echoed)} B back match={ok} | DUT {reply.strip()}")
            if not ok: failures.append(f"{baud} echo")
            # overflow: 512 bytes while the DUT is not reading for 50 ms (64-byte ring), then it drains
            link.drain(0.01); link.send("RECV 512 50\n"); time.sleep(0.005)
            uart2.write(lcg_bytes(512, 7)); reply = cmd("", "RECV n=", timeout=5) if False else None
            if not link.wait("RECV n=", 5): raise SystemExit("no RECV reply")
            reply = next(l for l in link.text.splitlines()[::-1] if "RECV n=" in l).strip()
            log(f"[{baud:>6} baud] overflow: 512 B sent while the DUT slept 50 ms -> DUT {reply} (ring is 64 B; losing bytes is expected, hanging is not)")
            # the P4 may still be transmitting (512 B at 9600 baud take 533 ms while RECV drains for 200 ms):
            # wait for the line to go quiet, then let the DUT drain the leftovers before the recovery echo
            deadline = time.perf_counter() + 512 * 10 / baud + 0.2
            while time.perf_counter() < deadline:
                uart2.read(256); link.drain(0.02)
            cmd("RECV 4096 0", "RECV n=")
            # recovery after overflow: a fresh 256-byte echo must still work
            payload = lcg_bytes(256, 99); link.drain(0.01); link.send("ECHO 256 2000\n"); time.sleep(0.05)
            echoed = bytearray()
            for off in range(0, 256, 32):
                uart2.write(payload[off:off + 32]); deadline = time.perf_counter() + 2.0
                while len(echoed) < off + 32 and time.perf_counter() < deadline:
                    chunk = uart2.read(256)
                    if chunk: echoed += chunk
            link.wait("ECHO n=", 3)
            ok = bytes(echoed) == payload
            log(f"[{baud:>6} baud] after overflow: echo 256 B match={ok}")
            if not ok: failures.append(f"{baud} recovery")
            cmd("CLOSE", "CLOSE ok")
        # long continuous transfer at 115200: 65536 bytes DUT -> P4
        cmd("OPEN 115200", "OPEN ok"); uart2.configure(115200); uart2.read(4096); time.sleep(0.05)
        n, seed = 65536, 0xBEEF
        link.drain(0.01); link.send(f"SEND {n} {seed}\n"); t0 = time.perf_counter()
        got = drain2(n, 20.0); dt = time.perf_counter() - t0; link.wait("SEND done", 5)
        ok = got == lcg_bytes(n, seed)
        log(f"[115200 long] DUT->P4 {n} B continuous: {len(got)} B match={ok} in {dt:.2f} s ({len(got) * 10 / dt / 1000:.1f} kbit/s)")
        if not ok: failures.append("long")
        # resume after a debug reset
        cmd("CLOSE", "CLOSE ok")
        report = session.reset()
        if not oep_smoke.sync(link, "uart_probe READY", 5): raise SystemExit("no READY/PONG after reset")
        cmd("OPEN 115200", "OPEN ok"); uart2.configure(115200); uart2.read(4096); time.sleep(0.05)
        cmd("SEND 512 77", "SEND done"); got = drain2(512, 3.0); ok = got == lcg_bytes(512, 77)
        log(f"[after reset] flags=0x{report.flags:02x} DUT->P4 512 B match={ok}")
        if not ok: failures.append("after reset")
    finally:
        session.close()
    log(f"failures={failures}")


if __name__ == "__main__":
    main()
