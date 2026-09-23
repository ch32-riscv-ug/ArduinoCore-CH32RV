# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""X035 USART2 against the probe's second fixture.uart (worklist P3 row 2): DUT -> P4 and P4 -> DUT
(echo) binary payloads at several bauds, a long continuous transfer, receive overflow behaviour of
the 64-byte ring, and resumption after a debug reset.

  uv run tests/manual/oep_uart_trace/oep_uart_trace.py [--bauds 9600,115200,460800]

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

SKETCH = HERE / "uart_probe"
UART2_RX, UART2_TX = 48, 49   # probe RX = DUT TX (PA2), probe TX = DUT RX (PA3)


def lcg_bytes(n: int, seed: int) -> bytes:
    x = seed & 0xFFFFFFFF
    out = bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=oep_smoke.DEFAULT_PORT)
    parser.add_argument("--fqbn", default="ch32-riscv-ug:ch32v:CH32X035:pnum=ANY")
    parser.add_argument("--oep-client", default=str(oep_smoke.DEFAULT_CLIENT))
    parser.add_argument("--bauds", default="9600,115200,460800")
    args = parser.parse_args()
    sys.path.insert(0, args.oep_client)
    from oep_client.v0 import codec
    from oep_client.v0.__main__ import open_client
    from oep_client.v0.flash_image import Target, program_image
    from oep_client.v0.services import FixtureUart

    log = print
    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build("uart_probe", args.fqbn, pathlib.Path(tmp), log, source=SKETCH)
        image = binary.read_bytes()
    client = open_client(args.port, 3.0)
    target = Target(client)
    outcome = program_image(target, image)
    log(f"programmed {outcome.pages_changed} pages verified={outcome.verified}")
    if not outcome.verified:
        raise SystemExit("program/verify failed")
    uarts = [f for f in client.list_functions() if (f.owner, f.id) == codec.DEF_FIXTURE_UART[:2]]
    if not uarts:
        raise SystemExit("probe offers no fixture.uart")
    uart2 = FixtureUart(client, uarts[0].function)
    lease, _ = client.plan_apply(uart2.assignments(rx=UART2_RX, tx=UART2_TX))
    failures = []
    try:
        link = oep_smoke.open_console(client)
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
        report = target.control.reset()
        if not oep_smoke.sync(link, "uart_probe READY", 5): raise SystemExit("no READY/PONG after reset")
        cmd("OPEN 115200", "OPEN ok"); uart2.configure(115200); uart2.read(4096); time.sleep(0.05)
        cmd("SEND 512 77", "SEND done"); got = drain2(512, 3.0); ok = got == lcg_bytes(512, 77)
        log(f"[after reset] flags=0x{report.flags:02x} DUT->P4 512 B match={ok}")
        if not ok: failures.append("after reset")
    finally:
        client.plan_release(lease)
        target.control.reset()
    log(f"failures={failures}")


if __name__ == "__main__":
    main()
