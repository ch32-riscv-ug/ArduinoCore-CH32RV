"""The UART the probe hears, both ways against the probe's fixture UART. The bench file's [uart] names the DUT's
USART and route; the host tells the sketch ("UART <n> <route> <baud>", testcmd.h) and opens the probe's side.

test_uart_bauds: DUT -> probe and probe -> DUT -> probe (echo) payloads at three bauds, receive overflow of the
64-byte ring and recovery, a 64 KiB continuous transfer, and resumption after the probe's debug reset.

test_uart_sweep (slow): begin() over F_CPU x baud x format - the standard bauds, the baud each small BRR rounds
worst, bauds out of range (begin() must leave the port closed), other formats, begin() on a running port, begin()
with bytes still leaving. Each is checked both ways and, where the capture allows, measured on the wire: the core
sets BRR = round(F_CPU / baud), so the real baud is F_CPU / BRR.

  uv run --env-file .env pytest bench/trace/uart_probe --profile ch32x035 -s
  uv run --env-file .env pytest bench/trace/uart_probe --profile ch32x035 -s -m slow
"""
import time

import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")
tk = load("tests/bench/tracekit.py", "tracekit")
import benchdef  # noqa: E402  (bench/ is on sys.path: conftest)
from wireskein import runlog as ws  # noqa: E402


def lcg_bytes(n: int, seed: int) -> bytes:
    x = seed & 0xFFFFFFFF
    out = bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


@pytest.fixture
def fx(request, dut, bench, ws_run):
    n, route = bench.uart
    kit.start(dut, "uart_probe")
    con = tk.Console(dut, ws_run)
    con.ask(f"UART {n} {route} 115200", "UART OK")
    oep_host = request.getfixturevalue("oep_host")
    f = tk.Fixture(oep_host.host, bench, ws_run, con)
    f.uart = tk.Uart(f)
    f.usart, f.route = n, route
    yield f
    f.release()


def cmd(fx, text: str, reply: str, timeout: float = 10.0) -> str:
    fx.console.drain(0.01)
    return fx.console.ask(text, reply, timeout)


def echo(fx, payload: bytes, step: int, per_step: float) -> bytes:
    """ECHO through the DUT in `step`-byte pieces, paced by reading the echo (the DUT ring is 64 bytes)."""
    fx.console.drain(0.01)
    fx.console.send(f"ECHO {len(payload)} 2000")
    time.sleep(0.05)
    echoed = bytearray()
    for off in range(0, len(payload), step):
        fx.uart.io.write(payload[off:off + step])
        deadline = time.perf_counter() + per_step
        while len(echoed) < off + step and time.perf_counter() < deadline:
            chunk = fx.uart.io.read(256)
            if chunk:
                echoed += chunk
    fx.console.wait("ECHO n=", 3)
    return bytes(echoed)


def test_uart_bauds(fx, ws_run):
    bad = []
    for baud in (9600, 115200, 460800):
        with ws_run.section(1, f"{baud} baud"):
            cmd(fx, f"OPEN {baud}", "OPEN ok")
            fx.uart.configure(baud)
            fx.uart.flush_input()
            time.sleep(0.05)
            # DUT -> probe, 4096 bytes
            n, seed = 4096, 0x1234 + baud
            t0 = time.perf_counter()
            cmd(fx, f"SEND {n} {seed}", "SEND done", 30)
            got = fx.uart.collect(n, 5.0)
            dt = time.perf_counter() - t0
            ok = got == lcg_bytes(n, seed)
            print(f"[{baud:>6} baud] DUT->probe {n} B: {len(got)} B received match={ok} ({n * 10 / dt / 1000:.1f} kbit/s incl. command)")
            if not ok:
                bad.append(f"{baud} dut->probe")
            # probe -> DUT -> probe, 2048 bytes in 32-byte pieces
            payload = lcg_bytes(2048, seed ^ 0x55)
            echoed = echo(fx, payload, 32, 2.0)
            ok = echoed == payload
            print(f"[{baud:>6} baud] probe->DUT->probe echo {len(payload)} B: {len(echoed)} B back match={ok}")
            if not ok:
                bad.append(f"{baud} echo")
            # overflow: 512 bytes while the DUT is not reading for 50 ms (64-byte ring), then it drains
            fx.console.drain(0.01)
            fx.console.send("RECV 512 50")
            time.sleep(0.005)
            fx.uart.io.write(lcg_bytes(512, 7))
            reply = fx.console.wait("RECV n=", 5)
            print(f"[{baud:>6} baud] overflow: 512 B while the DUT slept 50 ms -> {reply} (losing bytes is expected, hanging is not)")
            ws_run.note(f"{baud}: overflow {reply}")
            # the probe may still be transmitting: wait for the line to go quiet, then let the DUT drain
            deadline = time.perf_counter() + 512 * 10 / baud + 0.2
            while time.perf_counter() < deadline:
                fx.uart.io.read(256)
                fx.console.drain(0.02)
            cmd(fx, "RECV 4096 0", "RECV n=")
            # recovery after overflow: a fresh 256-byte echo must still work
            payload = lcg_bytes(256, 99)
            echoed = echo(fx, payload, 32, 2.0)
            ok = echoed == payload
            print(f"[{baud:>6} baud] after overflow: echo 256 B match={ok}")
            if not ok:
                bad.append(f"{baud} recovery")
            cmd(fx, "CLOSE", "CLOSE ok")
    # long continuous transfer at 115200: 65536 bytes DUT -> probe. A probe whose OEP link is itself a 115200 serial
    # port cannot carry that in full (the host takes ~8 KB/s, the UART brings ~11), so such a bench names what fits
    # the probe's ring (facts.uart_long_bytes) and the full length is judged on the benches with a USB link.
    with ws_run.section(1, "115200 long"):
        cmd(fx, "OPEN 115200", "OPEN ok")
        fx.uart.configure(115200)
        fx.uart.flush_input()
        time.sleep(0.05)
        n, seed = int(fx.bench.facts.get("uart_long_bytes", 65536)), 0xBEEF
        if n < 65536:
            ws_run.note(f"{n} bytes, not 65536: what this probe's fixture-UART ring holds (facts.uart_long_bytes)")
        fx.console.drain(0.01)
        fx.console.send(f"SEND {n} {seed}")
        t0 = time.perf_counter()
        got = fx.uart.collect(n, 20.0)
        dt = time.perf_counter() - t0
        fx.console.wait("SEND done", 5)
        ok = got == lcg_bytes(n, seed)
        print(f"[115200 long] DUT->probe {n} B continuous: {len(got)} B match={ok} in {dt:.2f} s ({len(got) * 10 / dt / 1000:.1f} kbit/s)")
        if not ok:
            bad.append("long")
        cmd(fx, "CLOSE", "CLOSE ok")
    # resume after the probe's debug reset
    with ws_run.section(1, "after debug reset"):
        from oep_client import riscv
        _wire, conn = benchdef.attach_slot(fx.bench, fx.host)
        riscv.RiscvDm(fx.host, conn).reset(confirm=False)
        kit.start(fx.console.dut, "uart_probe")
        cmd(fx, f"UART {fx.usart} {fx.route} 115200", "UART OK")
        cmd(fx, "OPEN 115200", "OPEN ok")
        fx.uart.configure(115200)
        fx.uart.flush_input()
        time.sleep(0.05)
        cmd(fx, "SEND 512 77", "SEND done")
        got = fx.uart.collect(512, 3.0)
        ok = got == lcg_bytes(512, 77)
        print(f"[after debug reset] DUT->probe 512 B match={ok}")
        if not ok:
            bad.append("after reset")
    assert not bad, f"UART wrong: {bad}"


# ---------------------------------------------------------------- the sweep

SWEEP_BAUDS = (9600, 19200, 38400, 57600, 74880, 115200, 230400, 250000, 460800, 500000, 921600, 1000000, 1500000,
               2000000, 3000000)
SWEEP_FORMATS = ("8E1", "8O1", "8N2", "7E1", "7O2")
WORST_BRR = (16, 17, 18, 20, 24, 32, 48, 64)                 # BRR values whose rounding is tried at its worst
SWITCH_SEQUENCE = ((115200, "8N1"), (1000000, "8N1"), (9600, "8E1"), (2000000, "8N1"), (57600, "8O1"),
                   (460800, "7E1"), (115200, "8N2"), (115200, "8N1"))   # begin() on a running port, up and down
PENDING_PAIRS = ((115200, 1000000), (1000000, 115200), (9600, 115200))  # begin(b2) with bytes still leaving at b1


def expect_brr(f_cpu: int, baud: int) -> tuple[int, float, float]:
    """The core's BRR (round(F_CPU / baud)), the baud it gives and its error against the ask."""
    brr = (f_cpu + baud // 2) // baud if baud else 0
    real = f_cpu / brr if brr else 0.0
    return brr, real, (real / baud - 1) if brr else float("nan")


def sweep_cases(f_cpu: int) -> list[tuple[int, str, str]]:
    cases = [(b, "8N1", "standard") for b in SWEEP_BAUDS]
    cases += [(int(f_cpu / (n + 0.49)), "8N1", f"worst rounding at BRR {n}") for n in WORST_BRR]
    cases += [(f_cpu // 12, "8N1", "above the range (BRR 12)"), (f_cpu // 8, "8N1", "above the range (BRR 8)"),
              (f_cpu // 65535 + 1, "8N1", "the slowest in range"), (f_cpu // 70000, "8N1", "below the range (BRR > 0xFFFF)"),
              (0, "8N1", "zero")]
    for fmt in SWEEP_FORMATS:
        cases += [(115200, fmt, "format"), (f_cpu // 16, fmt, "format at BRR 16")]
    return cases


@pytest.mark.slow
def test_uart_sweep(fx, bench, ws_run):
    from oep_client.fixture import FixtureUart
    from oep_client.host import Rejected, Unsupported
    tx_pad = bench.data["uart"].get("tx")
    cap_max = int(bench.facts.get("capture_max_hz", 0))
    cap = None
    if cap_max and tx_pad:
        cap = tk.Capture(fx)
        try:
            fx.plan(cap.assignments(tx_pad))          # observing the pad the fixture UART already listens to
        except Rejected as e:
            ws_run.note(f"no wire measurement: the capture cannot join the UART's pad ({e})")
            cap = None
    f_cpu = int(cmd(fx, "CLOCK", "CLOCK f_cpu=").split("=")[1])
    rows = []

    def case(n, baud, fmt, why, close=True):
        brr, real, err = expect_brr(f_cpu, baud)
        bits, parity, stop = int(fmt[0]), {"N": "none", "E": "even", "O": "odd"}[fmt[1]], int(fmt[2])
        mask = (1 << bits) - 1
        row = {"baud": baud, "format": fmt, "why": why, "brr": brr, "tx_ok": None, "rx_ok": None, "probe_baud": None}
        with ws_run.section(1, f"{baud} {fmt}"):
            ws_run.note(f"{why}: BRR {brr}, expected {real:.1f} baud ({err * 100:+.3f} %)" + ("" if brr >= 16 else ", below BRR 16"))
            opened = cmd(fx, f"OPEN {baud} {fmt}", "OPEN ")
            row["closed"] = "closed" in opened
            row["refusal_ok"] = row["closed"] == (not 16 <= brr <= 0xFFFF)   # refused exactly when out of range
            if row["closed"]:
                ws_run.note("begin() left the port closed")
                rows.append(row)
                print(f"[{baud:>7} {fmt}] BRR {brr:>5} ({why}) | begin() left the port closed -> "
                      f"{'ok (out of range)' if row['refusal_ok'] else 'BAD (in range)'}")
                return
            if not row["refusal_ok"]:
                rows.append(row)
                print(f"[{baud:>7} {fmt}] BRR {brr:>5} ({why}) | BAD: begin() opened the port out of range")
                cmd(fx, "CLOSE", "CLOSE ok")
                return
            seed = (n * 7919 + baud) & 0xFFFF
            try:
                row["probe_baud"] = fx.uart.configure(baud, FixtureUart.format_byte(bits, fmt[1], stop))
            except Unsupported:                       # beyond the probe's UART: the wire still measures the DUT's TX
                ws_run.note(f"the probe's UART does not run {baud} {fmt}: no data comparison")
            if row["probe_baud"] is not None:
                fx.uart.flush_input()
                time.sleep(0.02)
                with ws_run.section(2, "tx"):         # DUT -> probe: 256 bytes byte for byte (7-bit formats: the low 7 bits)
                    cmd(fx, f"SEND 256 {seed}", "SEND done", 5)
                    got = fx.uart.collect(256, 0.5 + 2560 / baud)
                want = bytes(b & mask for b in lcg_bytes(256, seed))
                got = bytes(b & mask for b in got)
                row["tx_ok"] = got == want
                row["tx_bad"] = sum(a != b for a, b in zip(got, want)) + abs(len(want) - len(got))
                with ws_run.section(2, "rx"):         # probe -> DUT -> probe: 64 bytes echoed in 16-byte steps
                    payload = bytes(b & mask for b in lcg_bytes(64, seed ^ 0x55))
                    echoed = echo(fx, payload, 16, 0.3 + 160 / baud)
                row["rx_ok"] = bytes(b & mask for b in echoed) == payload
            # the wire: the baud measured inside a burst (8 samples a bit at least). Below BRR 16 nothing is promised.
            # The capture has no trigger and the burst starts 20 ms or more after the arm (below), so the window
            # (130816 samples at most) has to be 40 ms or more: with 10 samples a bit that holds up to about
            # 300 kbaud; faster bauds are checked by data only until the probe has an edge trigger.
            if cap:
                rate = min(cap_max, max(1_000_000, 10 * baud))
                if rate >= 8 * baud:
                    cap.configure(rate, 130_816)
                if rate < 8 * baud or cap.rate < 8 * baud:
                    ws_run.note("the capture cannot run 8 samples a bit at this baud: the wire is not taken")
                elif cap.config.samples / cap.rate < 0.04:
                    ws_run.note(f"the capture holds {cap.config.samples / cap.rate * 1000:.0f} ms, less than the console's "
                                "way in: the wire is not taken")
                else:
                    fmt_kw = {"bits": bits, "parity": parity, "stop": stop}
                    # A sampler that falls behind (facts.capture_time_base_slips: the classic ESP32's) stretches the
                    # bit times it records and can miss a slow burst altogether, so on such a bench the wire is only
                    # captured and kept, with no check on it; the data checks above still judge the DUT.
                    if bench.facts.get("capture_time_base_slips", False):
                        want = None
                    elif brr >= 16:
                        want = ws.uart(tx_pad, real, None, tol_baud=0.015, max_errors=0, **fmt_kw)
                    else:
                        want = ws.uart(tx_pad, None, **fmt_kw)
                    with ws_run.section(2, "wire", expect=None if want is None else [want]):
                        window = cap.config.samples / cap.rate
                        fx.console.drain(0.01)
                        cap.arm()
                        # The decoder learns the idle level from the capture, so the burst has to start after a
                        # clear stretch of idle: a dozen bit times, and never less than the console's way in
                        # (ch32rv 0.12.4 delivers a command in ~2 ms; at 733 baud that is under two bits, and a
                        # back-to-back burst then has longer low runs than high ones).
                        time.sleep(max(0.02, 12 / baud))
                        fx.console.send(f"BURST {int(window * 1000) + 150} {seed}")
                        st = cap.wait(3.0 + window)
                        if st.flags & cap.COMPLETE:
                            cap.read_all()
                        fx.console.wait("BURST blocks=", 3)
                    if row["probe_baud"] is not None:
                        fx.uart.flush_input()          # the burst's bytes: only drained
            if close:
                cmd(fx, "CLOSE", "CLOSE ok")
        rows.append(row)
        v = {True: "ok", False: "BAD", None: "-"}
        print(f"[{baud:>7} {fmt}] BRR {brr:>5} expected {err * 100:+7.3f} %{' (BRR < 16)' if brr < 16 else ''} ({why})"
              f" | probe {row['probe_baud'] or 'cannot'} | DUT->probe {v[row['tx_ok']]}"
              f"{' (' + str(row.get('tx_bad')) + ' bytes)' if row['tx_ok'] is False else ''} | echo {v[row['rx_ok']]}")

    cases = sweep_cases(f_cpu)
    for n, (baud, fmt, why) in enumerate(cases):
        case(n, baud, fmt, why)
    for k, (baud, fmt) in enumerate(SWITCH_SEQUENCE):        # begin() again without end(), back and forth
        case(len(cases) + k, baud, fmt, "switched without end()", close=False)
    cmd(fx, "CLOSE", "CLOSE ok")
    for k, (b1, b2) in enumerate(PENDING_PAIRS):             # begin() while bytes are still going out
        with ws_run.section(1, f"begin {b2} while sending at {b1}"):
            seed = 0x4000 + k
            cmd(fx, f"OPEN {b1}", "OPEN ok")
            fx.uart.configure(b2)
            fx.uart.flush_input()
            time.sleep(0.02)
            reply = cmd(fx, f"SWITCH {b2} {seed}", "SWITCH done", 5)
            got = fx.uart.collect(64, 0.5)
            want = lcg_bytes(16, seed + 1)
            ok = got.endswith(want)
            rows.append({"baud": b2, "format": "8N1", "why": f"begin({b2}) while sending at {b1}", "brr": 0,
                         "tx_ok": ok, "rx_ok": None, "refusal_ok": True})
            ws_run.note(f"received {len(got)} bytes at {b2}: {got.hex()}; the block after begin() {'whole' if ok else 'NOT whole'}")
            print(f"[begin({b2}) while sending at {b1}] {len(got)} bytes at {b2}, the block after begin() "
                  f"{'came out whole' if ok else 'BAD'} | {reply}")
            cmd(fx, "CLOSE", "CLOSE ok")
    bad = [r for r in rows if r["tx_ok"] is False or r["rx_ok"] is False or r.get("refusal_ok") is False]
    print(f"cases {len(rows)}, wrong in {len(bad)}: " + ", ".join(f"{r['baud']} {r['format']} (BRR {r['brr']})" for r in bad))
    assert not bad, "UART sweep: " + ", ".join(f"{r['baud']} {r['format']} ({r['why']})" for r in bad)
