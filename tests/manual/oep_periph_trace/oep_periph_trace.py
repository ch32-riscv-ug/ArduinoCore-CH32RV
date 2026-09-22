# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Measure X035 peripheral outputs on the wire with the OEP probe's fixture.capture: analogWrite
PWM frequency and duty, tone() frequency, delayMicroseconds()/millis() timing, and SPI master
mode/clock/data (worklist P3 rows 4, 5, 7). The sketch periph_probe is programmed first.

  uv run tests/manual/oep_periph_trace/oep_periph_trace.py            # all sections
  uv run tests/manual/oep_periph_trace/oep_periph_trace.py --only spi

Fixture (E143 pin map): PA1 -> P4 GPIO47, SPI1 SCK PA5 -> 4, MOSI PA7 -> 5, MISO PA6 -> 11, CS PA4 -> 53.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "oep_smoke"))
import oep_smoke  # noqa: E402
import targets  # noqa: E402

SKETCH = HERE / "periph_probe"
PWM_GPIO, SCK, MOSI, MISO, CS = 47, 4, 5, 11, 53
UART_RX, UART_TX = 12, 6


def edges(samples: bytes, bit: int):
    prev = (samples[0] >> bit) & 1
    out = []
    for i in range(1, len(samples)):
        cur = (samples[i] >> bit) & 1
        if cur != prev:
            out.append((i, cur))
            prev = cur
    return out


def square_stats(samples: bytes, bit: int, rate: int):
    """period / duty statistics of a square wave from its edges."""
    e = edges(samples, bit)
    rises = [i for i, lvl in e if lvl == 1]
    falls = [i for i, lvl in e if lvl == 0]
    if len(rises) < 3:
        return None
    periods = [b - a for a, b in zip(rises, rises[1:])]
    highs = []
    for r in rises:
        f = next((x for x in falls if x > r), None)
        if f is not None:
            highs.append(f - r)
    period = statistics.median(periods)
    high = statistics.median(highs) if highs else 0
    return {"edges": len(e), "period_us": period / rate * 1e6, "freq_hz": rate / period, "duty": high / period,
            "period_jitter_us": (max(periods) - min(periods)) / rate * 1e6}


def decode_spi(samples: bytes, rate: int):
    """Decode one CS-framed SPI transfer. Returns dict: mosi per capture edge (rise/fall), SCK idle
    level right before CS fell (CPOL), SCK frequency, and the CS-low duration."""
    prev = samples[0]
    bits = {"rise": [], "fall": []}
    miso_bits = {"rise": [], "fall": []}
    sck_rises = []
    cs_fall = None
    cs_rise = None
    for i in range(1, len(samples)):
        cur = samples[i]
        cs_prev, cs_cur = (prev >> 3) & 1, (cur >> 3) & 1
        if cs_prev and not cs_cur and cs_fall is None:
            cs_fall = i
        if not cs_prev and cs_cur and cs_fall is not None and cs_rise is None:
            cs_rise = i
        if cs_fall is not None and cs_rise is None:
            sck_prev, sck_cur = prev & 1, cur & 1
            if sck_prev != sck_cur:
                bits["rise" if sck_cur else "fall"].append((cur >> 1) & 1)
                miso_bits["rise" if sck_cur else "fall"].append((cur >> 2) & 1)
                if sck_cur:
                    sck_rises.append(i)
        prev = cur
    def pack(b):
        return bytes(int("".join(map(str, b[k:k + 8])), 2) for k in range(0, len(b) - len(b) % 8, 8))
    sck_hz = rate / statistics.median([b - a for a, b in zip(sck_rises, sck_rises[1:])]) if len(sck_rises) > 2 else 0
    # where does MOSI change relative to the nearest SCK edge (rise or fall)?
    prev = samples[0]; sck_edges = []; mosi_changes = []
    for i in range(1, len(samples)):
        cur = samples[i]
        if cs_fall is not None and (cs_rise is None or i < cs_rise) and i > cs_fall:
            if (prev ^ cur) & 1: sck_edges.append((i, "rise" if cur & 1 else "fall"))
            if (prev ^ cur) & 2: mosi_changes.append(i)
        prev = cur
    near = {"rise": 0, "fall": 0}
    for m in mosi_changes:
        if sck_edges:
            e = min(sck_edges, key=lambda x: abs(x[0] - m)); near[e[1]] += 1
    idle = (samples[cs_fall - 1] & 1) if cs_fall and cs_fall > 0 else None
    return {"mosi_rise": pack(bits["rise"]), "mosi_fall": pack(bits["fall"]), "miso_rise": pack(miso_bits["rise"]), "miso_fall": pack(miso_bits["fall"]),
            "cpol": idle, "sck_hz": sck_hz, "mosi_changes_near": near,
            "clocks": len(sck_rises), "cs_low_us": ((cs_rise or len(samples)) - cs_fall) / rate * 1e6 if cs_fall is not None else None}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    targets.add_target_argument(parser)
    parser.add_argument("--port", help="probe serial port (default: the target profile's)")
    parser.add_argument("--fqbn", help="DUT board (default: the target profile's)")
    parser.add_argument("--oep-client", default=str(oep_smoke.DEFAULT_CLIENT))
    parser.add_argument("--only", choices=["pwm", "tone", "timing", "spi", "spi-peer"], action="append")
    parser.add_argument("--result-json")
    args = parser.parse_args()
    profile = targets.TARGETS[args.target]
    global PWM_GPIO, SCK, MOSI, MISO, CS, UART_RX, UART_TX
    PWM_GPIO, UART_RX, UART_TX = profile["pwm"], profile["uart_rx"], profile["uart_tx"]
    SCK, MOSI, MISO, CS = profile["spi"]["sck"], profile["spi"]["mosi"], profile["spi"]["miso"], profile["spi"]["cs"]
    has_capture = profile["capture"]
    cap_max = profile.get("capture_max_hz", 0)
    decode_ok = has_capture and cap_max >= 5_000_000     # SPI wire decode needs >= 5 samples per SCK period
    sections = args.only or (["pwm", "tone", "timing", "spi", "spi-peer"] if decode_ok else ["pwm", "tone", "timing", "spi-peer"])
    if not has_capture and any(sec != "spi-peer" for sec in sections):
        raise SystemExit("this probe has no fixture.capture: only spi-peer runs on " + args.target)
    sys.path.insert(0, args.oep_client)
    from oep_client.v0 import codec
    from oep_client.v0.__main__ import open_client
    from oep_client.v0.flash_image import Target, program_image
    from oep_client.v0.services import FixtureCapture, FixtureUart, P4SpiTarget

    log = print
    with tempfile.TemporaryDirectory() as tmp:
        binary = oep_smoke.build("periph_probe", args.fqbn or profile["fqbn"], profile["serial_index"], pathlib.Path(tmp), log,
                                 source=SKETCH, defines=targets.build_defines(profile))
        image = binary.read_bytes()
    client = open_client(args.port or profile["port"], 3.0)
    target = Target(client)
    outcome = program_image(target, image)
    log(f"programmed {outcome.pages_changed} pages verified={outcome.verified}")
    if not outcome.verified:
        raise SystemExit("program/verify failed")
    uart = FixtureUart(client, client.find(*codec.DEF_FIXTURE_UART[:2]).function)
    capture = FixtureCapture(client, client.find(*codec.DEF_FIXTURE_CAPTURE[:2]).function) if has_capture else None
    results = {}

    def run(cap_lines, rate, samples, command, reply, settle=0.05):
        """Configure the capture, send a command, capture after `settle`, return (samples, reply line)."""
        capture.configure(rate, samples)
        link.drain(0.05)
        link.send(command + "\n")
        got = link.wait(reply, 5)
        time.sleep(settle)
        capture.arm()
        st = capture.wait(3.0)
        data = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
        line = next((l for l in link.text.splitlines()[::-1] if reply in l), "")
        return data, line.strip(), got

    def session(cap_lines):
        lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX) + capture.assignments(*cap_lines))
        uart.configure(115200)
        lnk = oep_smoke.UartLink(uart)
        if not lnk.wait("periph_probe READY", 10):
            raise SystemExit(f"no READY: {lnk.text[:200]!r}")
        return lease, lnk

    if any(s in sections for s in ("pwm", "tone", "timing")):
        lease, link = session((PWM_GPIO,))
        try:
            if "pwm" in sections:
                rows = []
                for duty in (64, 128, 192, 255, 0):
                    data, line, _ = run((PWM_GPIO,), 2_000_000, 40_000, f"PWM {duty}", "PWM duty=")
                    st = square_stats(data, 0, 2_000_000)
                    level = sum(d & 1 for d in data) / len(data) if data else None
                    row = {"duty": duty, "stats": st, "high_fraction": level}
                    rows.append(row)
                    if st:
                        log(f"[pwm duty={duty:3d}] f={st['freq_hz']:.1f} Hz period={st['period_us']:.2f} us duty={st['duty']*100:.1f}% (expected {duty/255*100:.1f}%) jitter={st['period_jitter_us']:.2f} us")
                    else:
                        log(f"[pwm duty={duty:3d}] no edges, level high fraction={level:.2f} (expected {'1.00' if duty == 255 else '0.00'})")
                results["pwm"] = rows
            if "tone" in sections:
                rows = []
                for hz in (500, 1000, 4000):
                    data, line, _ = run((PWM_GPIO,), 2_000_000, 60_000, f"TONE {hz}", "TONE hz=")
                    st = square_stats(data, 0, 2_000_000)
                    rows.append({"hz": hz, "stats": st})
                    log(f"[tone {hz} Hz] measured f={st['freq_hz']:.1f} Hz duty={st['duty']*100:.1f}% jitter={st['period_jitter_us']:.2f} us" if st else f"[tone {hz}] no edges")
                run((PWM_GPIO,), 2_000_000, 1000, "NOTONE", "NOTONE")
                results["tone"] = rows
            if "timing" in sections:
                rows = []
                for us in (0, 10, 100, 1000):
                    capture.configure(2_000_000, 60_000); link.drain(0.05); capture.arm()
                    link.send(f"TOGGLE {us} 20\n"); link.wait("TOGGLE done", 5)
                    st = capture.wait(3.0); data = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                    s = square_stats(data, 0, 2_000_000)
                    rows.append({"delay_us": us, "stats": s})
                    log(f"[delayMicroseconds({us})] half period measured {s['period_us']/2:.2f} us (edges {s['edges']}, jitter {s['period_jitter_us']:.2f} us)" if s else f"[delayMicroseconds({us})] no edges")
                capture.configure(2_000_000, 60_000); link.drain(0.05); capture.arm()
                link.send("TOGGLE0 20\n"); link.wait("TOGGLE0 done", 5)
                st = capture.wait(3.0); data = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                s = square_stats(data, 0, 2_000_000)
                log(f"[digitalWrite pair, no delay] half period {s['period_us']/2:.2f} us (edges {s['edges']})" if s else "[digitalWrite pair] no edges")
                capture.configure(1_000_000, 400_000); link.drain(0.05); capture.arm()   # 0.4 s window: PARLIO cannot sample below ~650 kHz
                link.send("MILLIS 10 20\n"); link.wait("MILLIS done", 5)
                log("   DUT: " + next((l for l in link.text.splitlines()[::-1] if "MILLIS done" in l), "").strip())
                st = capture.wait(3.0); data = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                s = square_stats(data, 0, 1_000_000)
                rows.append({"millis_ms": 10, "stats": s})
                log(f"[millis() toggle every 10 ms] period measured {s['period_us']/1000:.3f} ms (expected 20.000), jitter {s['period_jitter_us']:.1f} us" if s
                    else f"[millis] no edges: samples={len(data)} high fraction={(sum(d & 1 for d in data) / len(data)) if data else None} flags=0x{st.flags:02x}")
                results["timing"] = rows
        finally:
            client.plan_release(lease)

    if "spi" in sections:
        lease, link = session((SCK, MOSI, MISO, CS))
        try:
            rows = []
            payload = bytes.fromhex("a55a0f01")
            for hz, mode in ((1_000_000, 0), (1_000_000, 1), (1_000_000, 2), (1_000_000, 3), (4_000_000, 0), (250_000, 0)):
                rate = min(20_000_000 if hz >= 1_000_000 else 5_000_000, cap_max)
                capture.configure(rate, 8 * 65_000 // 4); link.drain(0.05); capture.arm()
                link.send(f"SPI {hz} {mode} {payload.hex()}\n"); link.wait("SPI got=", 5)
                st = capture.wait(3.0); data = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                line = next((l for l in link.text.splitlines()[::-1] if "SPI got=" in l), "").strip()
                d = decode_spi(data, rate)
                cpol, cpha = (mode >> 1) & 1, mode & 1
                # CPHA=0: MOSI changes on the trailing edge (falling for CPOL=0, rising for CPOL=1) and is
                # sampled on the leading edge; CPHA=1: MOSI changes on the leading edge.
                leading = "rise" if cpol == 0 else "fall"
                trailing = "fall" if cpol == 0 else "rise"
                change_edge_expected = leading if cpha else trailing
                near = d["mosi_changes_near"]
                change_edge_seen = "rise" if near["rise"] > near["fall"] else "fall"
                data_ok = payload in (d["mosi_rise"], d["mosi_fall"])
                ok = d["cpol"] == cpol and change_edge_seen == change_edge_expected and data_ok
                rows.append({"hz": hz, "mode": mode, "cpol_seen": d["cpol"], "change_edge_expected": change_edge_expected,
                             "change_edge_seen": change_edge_seen, "mosi_changes_near": near, "data_ok": data_ok,
                             "sck_hz": d["sck_hz"], "clocks": d["clocks"], "cs_low_us": d["cs_low_us"], "dut": line})
                log(f"[spi {hz} Hz mode {mode}] CPOL={d['cpol']} (expect {cpol}), MOSI changes on {change_edge_seen} edge (expect {change_edge_expected}, counts {near}), data={'ok' if data_ok else 'BAD'} -> {'OK' if ok else 'MISMATCH'} | "
                    f"SCK {d['sck_hz']/1e6:.3f} MHz clocks={d['clocks']} CS low {d['cs_low_us']:.1f} us | DUT: {line}")
            results["spi"] = rows
        finally:
            client.plan_release(lease)
            target.control.reset()
    if "spi-peer" in sections:
        # P3 row 7, MISO side: the P4 SPI slave (p4.spi-target, SPI2_HOST) answers on MISO while the capture
        # observes all four lines in the same plan. Checks per mode: DUT got == preloaded MISO bytes, target's
        # MOSI bytes == DUT payload, and the wire decode of both lines agrees.
        spi = P4SpiTarget(client, client.find(*codec.DEF_P4_SPI_TARGET[:2]).function)
        lease, _ = client.plan_apply(uart.assignments(rx=UART_RX, tx=UART_TX) + spi.assignments(sck=SCK, mosi=MOSI, miso=MISO, cs=CS)
                                     + (capture.assignments(SCK, MOSI, MISO, CS) if has_capture else []))
        uart.configure(115200); link = oep_smoke.UartLink(uart); link.send("\n")
        if not link.wait("periph_probe READY", 10): raise SystemExit(f"no READY: {link.text[:200]!r}")
        try:
            rows = []
            payload, answer = bytes.fromhex("a55a0f01"), bytes.fromhex("3c96c30f")
            # Warm-up before arming: the DUT's first SPI command runs SPI.begin() and pinMode(CS), and the CS
            # pin floating until then let the slave see a spurious frame that consumed the armed transaction
            # (first run: target rx empty, bits=0, while the DUT still got the FIFO's answer).
            spi.configure(0); link.drain(0.05); link.send(f"SPI 1000000 0 {payload.hex()}\n"); link.wait("SPI got=", 5); time.sleep(0.05)
            for hz, mode in ((1_000_000, 0), (1_000_000, 1), (1_000_000, 2), (1_000_000, 3), (250_000, 0), (4_000_000, 0), (4_000_000, 3),
                             (12_000_000, 0), (12_000_000, 3), (24_000_000, 0)):
                spi.configure(mode); spi.arm(len(payload), answer)
                rate = min(20_000_000 if hz >= 1_000_000 else 5_000_000, cap_max or 1)
                if has_capture: capture.configure(rate, 8 * 65_000 // 4)
                link.drain(0.05)
                if has_capture: capture.arm()
                link.send(f"SPI {hz} {mode} {payload.hex()}\n"); link.wait("SPI got=", 5)
                data = b""
                if has_capture:
                    st = capture.wait(3.0); data = capture.read_all(st.samples) if st.flags & FixtureCapture.COMPLETE else b""
                line = next((l for l in link.text.splitlines()[::-1] if "SPI got=" in l), "").strip()
                got = bytes.fromhex(line.split("got=")[1]) if "got=" in line else b""
                time.sleep(0.05); pending, bits, rx = spi.read_rx()
                d = decode_spi(data, rate) if data else {"miso_rise": b"", "miso_fall": b"", "mosi_rise": b"", "mosi_fall": b"", "sck_hz": 0.0}
                cpha = mode & 1; cpol = (mode >> 1) & 1
                sample_edge = ("fall" if cpol == 0 else "rise") if cpha else ("rise" if cpol == 0 else "fall")   # leading edge samples for CPHA=0
                wire_miso, wire_mosi = d["miso_" + sample_edge], d["mosi_" + sample_edge]
                wire_ok = (wire_miso == answer and wire_mosi == payload) if (decode_ok and hz <= 4_000_000) else True   # 20 MS/s cannot decode the 6/12 MHz SCK
                ok = got == answer and rx == payload and bits == len(payload) * 8 and wire_ok
                rows.append({"hz": hz, "mode": mode, "dut_got": got.hex(), "target_rx": rx.hex(), "bits": bits, "wire_miso": wire_miso.hex(),
                             "wire_mosi": wire_mosi.hex(), "sck_hz": d["sck_hz"], "ok": ok})
                log(f"[spi peer {hz} Hz mode {mode}] DUT got={got.hex()} (expect {answer.hex()}) | target rx={rx.hex()} bits={bits} (expect {payload.hex()}) "
                    f"| wire MISO={wire_miso.hex()} MOSI={wire_mosi.hex()} SCK {d['sck_hz']/1e6:.3f} MHz -> {'OK' if ok else 'BAD'}")
            # Continuous / long transfers (P3 row 7 remainder): 64-byte transactions back to back, and a burst
            # of five 4-byte transactions with one arm each. The DUT sketch caps a transfer at 64 bytes.
            import random
            rnd = random.Random(7)
            long_payload = bytes(rnd.randrange(256) for _ in range(64)); long_answer = bytes(rnd.randrange(256) for _ in range(64))
            for n in range(3):
                spi.configure(0); spi.arm(64, long_answer); link.drain(0.02)
                link.send(f"SPI 1000000 0 {long_payload.hex()}\n"); link.wait("SPI got=", 5)
                line = next((l for l in link.text.splitlines()[::-1] if "SPI got=" in l), "").strip()
                got = bytes.fromhex(line.split("got=")[1]) if "got=" in line else b""
                time.sleep(0.05); pending, bits, rx = spi.read_rx()
                ok = got == long_answer and rx == long_payload and bits == 512
                rows.append({"long": n, "ok": ok, "bits": bits})
                log(f"[spi peer 64-byte transfer #{n}] DUT MISO {'match' if got == long_answer else 'MISMATCH'} | target MOSI {'match' if rx == long_payload else 'MISMATCH'} bits={bits} -> {'OK' if ok else 'BAD'}")
            burst_ok = 0
            spi.configure(3)
            for n in range(5):
                spi.arm(4, answer); link.send(f"SPI 4000000 3 {payload.hex()}\n"); link.wait("SPI got=", 5)
                line = next((l for l in link.text.splitlines()[::-1] if "SPI got=" in l), "").strip()
                got = bytes.fromhex(line.split("got=")[1]) if "got=" in line else b""
                pending, bits, rx = spi.read_rx()
                burst_ok += got == answer and rx == payload
            rows.append({"burst5": burst_ok})
            log(f"[spi peer burst, 5 x 4-byte at 4 MHz mode 3, no settle] {burst_ok}/5 both ways")
            results["spi_peer"] = rows
        finally:
            client.plan_release(lease)
            target.control.reset()
    if args.result_json:
        pathlib.Path(args.result_json).write_text(json.dumps(results, indent=1, default=str))
        log(f"wrote {args.result_json}")


if __name__ == "__main__":
    main()
