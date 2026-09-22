# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Reset and startup timing of the X035 core through the OEP probe (worklist P3 row 9), using the
basic system_selftest sketch: software reset (CH32.restart) -> first banner byte, measured on the
console TX line with fixture.capture; reset_reason after a software reset and after the probe's
debug reset (ndmreset); debug reset -> banner latency as seen from the host (reference only, the
probe's reset sequence adds its own delays).

  uv run tests/manual/oep_reset_trace/oep_reset_trace.py
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "oep_smoke"))
import oep_smoke  # noqa: E402

CONSOLE_RX, CONSOLE_TX = 12, 6
RATE = 1_000_000


def tx_edges(samples: bytes):
    prev = samples[0] & 1
    out = []
    for i in range(1, len(samples)):
        cur = samples[i] & 1
        if cur != prev:
            out.append(i); prev = cur
    return out


def quiet_gap(samples: bytes, min_gap_us: int = 2_000):
    """Longest run without TX edges (the reboot): returns (start_idx, end_idx) in samples."""
    edges = tx_edges(samples)
    best = (0, 0)
    for a, b in zip(edges, edges[1:]):
        if b - a > best[1] - best[0]:
            best = (a, b)
    return best if best[1] - best[0] >= min_gap_us else None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default=oep_smoke.DEFAULT_PORT)
    parser.add_argument("--fqbn", default="ch32-riscv-ug:ch32v:CH32X035:pnum=ANY")
    parser.add_argument("--oep-client", default=str(oep_smoke.DEFAULT_CLIENT))
    parser.add_argument("--repeat", type=int, default=5)
    args = parser.parse_args()
    sys.path.insert(0, args.oep_client)
    from oep_client.v0 import codec
    from oep_client.v0.__main__ import open_client
    from oep_client.v0.flash_image import Target, program_image
    from oep_client.v0.services import FixtureCapture, FixtureGpio, FixtureUart

    log = print
    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build("reset_probe", args.fqbn, 4, pathlib.Path(tmp), log, source=HERE / "reset_probe")
        image = binary.read_bytes()
    client = open_client(args.port, 3.0)
    target = Target(client)
    outcome = program_image(target, image)
    log(f"programmed {outcome.pages_changed} pages verified={outcome.verified}")
    uart = FixtureUart(client, client.find(*codec.DEF_FIXTURE_UART[:2]).function)
    gpio = FixtureGpio(client, client.find(*codec.DEF_FIXTURE_GPIO[:2]).function)
    capture = FixtureCapture(client, client.find(*codec.DEF_FIXTURE_CAPTURE[:2]).function)
    MARK = 47   # X035 PA1 -> P4 GPIO47
    # plan: console + capture observing the marker and the console TX; fixture.gpio takes no plan roles (configure claims the pin)
    lease, _ = client.plan_apply(uart.assignments(rx=CONSOLE_RX, tx=CONSOLE_TX)
                                 + capture.assignments(MARK, CONSOLE_RX))
    try:
        uart.configure(115200)
        gpio.configure(MARK, FixtureGpio.INPUT_PULL_DOWN)
        link = oep_smoke.UartLink(uart)
        if not link.wait("reset_probe READY", 10):
            raise SystemExit(f"no READY: {link.text[:200]!r}")

        def reason():
            link.drain(0.02); mark = len(link.text); link.send("REASON\n"); link.wait("REASON ", 3)
            m = re.search(r"REASON (\w+)", link.text[mark:]); return m.group(1) if m else "?"

        def low_pulse(samples, bit=0):
            """(start, end) of the longest low run on `bit`, in samples."""
            best = (0, 0); start = None
            for i, b in enumerate(samples):
                lvl = (b >> bit) & 1
                if not lvl and start is None: start = i
                if lvl and start is not None:
                    if i - start > best[1] - best[0]: best = (start, i)
                    start = None
            if start is not None and len(samples) - start > best[1] - best[0]: best = (start, len(samples))
            return best

        log(f"[after program_image (debug reset)] reset_reason={reason()}")
        capture.configure(RATE, 8 * 65_000 // 2)   # 2 lines -> 0.26 s window
        sw = []
        for i in range(args.repeat):
            link.drain(0.05); capture.arm(); time.sleep(0.02); link.send("REBOOT\n")
            link.wait("reset_probe READY", 5)
            st = capture.wait(2.0); data = capture.read_all(st.samples)
            a, b = low_pulse(data, 0)
            sw.append((b - a) / RATE * 1000)
            log(f"[software reset {i + 1}] PA1 low (restart -> setup) {sw[-1]:.3f} ms; marker ends at {b/RATE*1000:.1f} ms of {len(data)/RATE*1000:.0f}")
        log(f"[software reset] reset_reason={reason()}")
        dbg = []
        for i in range(args.repeat):
            link.drain(0.05); capture.arm(); time.sleep(0.02); report = target.control.reset(confirm=False)
            link.wait("reset_probe READY", 5)
            st = capture.wait(2.0); data = capture.read_all(st.samples)
            a, b = low_pulse(data, 0)
            dbg.append((b - a) / RATE * 1000)
            log(f"[debug reset {i + 1}] flags=0x{report.flags:02x} PA1 low (ndmreset -> setup) {dbg[-1]:.3f} ms")
        log(f"[debug reset] reset_reason={reason()}")
        for name, v in (("software", sw), ("debug", dbg)):
            v = sorted(v)
            log(f"{name} reset -> setup(): median {v[len(v)//2]:.3f} ms, min {v[0]:.3f}, max {v[-1]:.3f} (n={len(v)})")
    finally:
        client.plan_release(lease)
        target.control.reset()


if __name__ == "__main__":
    main()
