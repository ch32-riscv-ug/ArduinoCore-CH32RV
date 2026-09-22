# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Compile a test sketch, program it through the OEP development probe, drive it over the
probe's fixture UART, and judge it the way smoke.py does - without WCH-LinkE or probe-rs.

  uv run tests/manual/oep_smoke/oep_smoke.py --sketch core_api
  uv run tests/manual/oep_smoke/oep_smoke.py --sketch all --result-json /tmp/oep-smoke.json

Bench facts (ESP32-P4 + CH32X035F8U6 fixture, 2026-09-22): the DUT console is USART4
(PB0 TX -> P4 GPIO12, PB1 RX <- P4 GPIO6), so sketches build with CH32_SERIAL_DEFAULT=4 and the
probe leases fixture.uart rx=12 tx=6. The OEP client comes from the sibling oep-client-python
checkout (--oep-client). The probe firmware must already be flashed (it is a separate build).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASIC = REPO / "tests" / "sketches" / "basic"
sys.path.insert(0, str(REPO / "tests" / "sketches"))
sys.path.insert(0, str(REPO / "tests" / "manual" / "smoke"))
from stage import stage_sketch                      # noqa: E402
from smoke import expectations, sketchbook          # noqa: E402  (pure helpers)

DEFAULT_PORT = "/run/board-identify/by-id/esp32-series-30eda0e31108"
DEFAULT_CLIENT = REPO.parents[1] / "dev_oep" / "oep-client-python" / "src"


def toolchain_bin() -> pathlib.Path:
    candidates = sorted((REPO / ".tools" / "xpack-riscv-none-elf-gcc").glob("*/bin"))
    if not candidates:
        raise SystemExit("no xPack RISC-V toolchain under .tools/")
    return candidates[-1]


def build(name: str, fqbn: str, serial_index: int, tmp: pathlib.Path, log) -> pathlib.Path:
    sketch_dir = stage_sketch(BASIC / name, tmp / name)
    out = tmp / "build"
    cmd = ["arduino-cli", "compile", "--fqbn", fqbn,
           "--build-property", f"compiler.path={toolchain_bin()}/",
           "--build-property", f"build.extra_flags=-DCH32_SERIAL_DEFAULT={serial_index}",
           "--build-path", str(out), str(sketch_dir)]
    r = subprocess.run(cmd, capture_output=True, text=True, env=sketchbook(tmp))
    if r.returncode:
        raise RuntimeError("compile failed\n" + r.stdout + r.stderr)
    log("   " + r.stdout.strip().splitlines()[0])
    bins = list(out.glob("*.ino.bin"))
    if len(bins) != 1:
        raise RuntimeError(f"expected one .ino.bin, found {bins}")
    return bins[0]


class UartLink:
    """expect() over fixture.uart: polls the probe and keeps a cursor like smoke.py's Link."""

    def __init__(self, uart):
        self.uart = uart
        self.text = ""
        self.cursor = 0

    def pump(self) -> None:
        data = self.uart.read(512)
        if data:
            self.text += data.decode("utf-8", errors="replace")

    def send(self, line: str) -> None:
        self.uart.write(line.encode())

    def wait(self, text: str, timeout: float, regex: bool = False) -> bool:
        deadline = time.monotonic() + timeout
        pattern = re.compile(text) if regex else None
        while True:
            haystack = self.text[self.cursor:]
            m = pattern.search(haystack) if pattern else None
            idx = m.end() if m else haystack.find(text) + (len(text) if text in haystack else 0) if not pattern else -1
            if (pattern and m) or (not pattern and text in haystack):
                self.cursor += idx
                return True
            if time.monotonic() >= deadline:
                return False
            self.pump()
            time.sleep(0.01)

    def drain(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.pump()
            time.sleep(0.02)


def judge(name: str, script: tuple, text: str, missed: list) -> dict:
    if missed:
        return {"verdict": "fail", "why": f"never arrived: {missed}"}
    if "FAIL" in text:
        return {"verdict": "fail", "why": f"the sketch reported {[ln for ln in text.splitlines() if 'FAIL' in ln]}"}
    counts = re.findall(r"failures=(\d+)", text)
    if counts and any(c != "0" for c in counts):
        return {"verdict": "fail", "why": f"reported failures={counts}"}
    replayed = sum(1 for verb, _ in script if verb != "write")
    if not replayed and not counts:
        return {"verdict": "skip", "why": "nothing to check against"}
    return {"verdict": "pass", "why": ", ".join(p for p in (f"{replayed} expectations" if replayed else "",
                                                          f"failures={counts[-1]}" if counts else "") if p)}


def run_one(name: str, args, client, target, uart_service, log) -> dict:
    from oep_client.v0 import codec
    from oep_client.v0.flash_image import program_image
    script = expectations(name)
    result = {"sketch": name}
    with tempfile.TemporaryDirectory() as tmp:
        t0 = time.perf_counter()
        try:
            binary = build(name, args.fqbn, args.serial_index, pathlib.Path(tmp), log)
        except RuntimeError as e:
            log(str(e))
            return {**result, "verdict": "fail", "why": "compile failed"}
        image = binary.read_bytes()
        result["image_bytes"] = len(image)
        result["sha256"] = hashlib.sha256(image).hexdigest()
        result["compile_s"] = round(time.perf_counter() - t0, 3)
        t0 = time.perf_counter()
        outcome = program_image(target, image)   # preflight, diff, pipelined pages, CRC verify, reset
        result["program"] = outcome.as_dict()
        result["program_s"] = round(time.perf_counter() - t0, 3)
        if not outcome.verified:
            return {**result, "verdict": "fail", "why": "program/verify failed"}
        log(f"   programmed {outcome.pages_changed} pages, verified, {result['program_s']} s")

    if args.diag:
        from oep_client.v0.services import FixtureGpio
        gpio = FixtureGpio(client, client.find(*codec.DEF_FIXTURE_GPIO[:2]).function)
        gpio.configure(args.uart_rx, FixtureGpio.INPUT_FLOATING)
        levels = [gpio.read(args.uart_rx) for _ in range(20)]
        log(f"   diag: P4 GPIO{args.uart_rx} (DUT TX) samples = {levels}")
    lease, _ = client.plan_apply(uart_service.assignments(rx=args.uart_rx, tx=args.uart_tx))
    try:
        uart_service.configure(args.baud)
        link = UartLink(uart_service)
        banner = f"{name} READY"
        if not link.wait(banner, args.seconds + 10):
            link.drain(0.5)
            log(f"   received {len(link.text)} chars: {link.text[:300]!r}")
            return {**result, "verdict": "fail", "why": f"no '{banner}'", "output": link.text}
        token = random.randrange(1000, 9999)
        link.send(f"PING {token}\n")
        if not link.wait(f"PONG {token}", 5):
            return {**result, "verdict": "fail", "why": "banner but no PONG", "output": link.text}
        missed = []
        if not any(verb == "write" for verb, _ in script):
            link.send("RUN\n")
        for verb, text in script:
            if verb == "write":
                link.send(text if text.endswith("\n") else text + "\n")
            elif verb == "expect":
                if not link.wait(text, args.seconds):
                    missed.append(text)
            elif verb == "match":
                if not link.wait(text, args.seconds, regex=True):
                    missed.append(text)
        link.wait(f"{name} done failures=", args.seconds)
        link.drain(0.3)
        verdict = judge(name, script, link.text, missed)
        return {**result, **verdict, "output": link.text}
    finally:
        client.plan_release(lease)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=os.environ.get("OEP_PROBE_PORT", DEFAULT_PORT))
    parser.add_argument("--oep-client", default=str(DEFAULT_CLIENT))
    parser.add_argument("--sketch", default="core_api", help="case name under tests/sketches/basic, or 'all'")
    parser.add_argument("--fqbn", default="ch32-riscv-ug:ch32v:CH32X035:pnum=ANY")
    parser.add_argument("--serial-index", type=int, default=4)
    parser.add_argument("--uart-rx", type=int, default=12)
    parser.add_argument("--uart-tx", type=int, default=6)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=4.0)
    parser.add_argument("--result-json")
    parser.add_argument("--diag", action="store_true", help="print DUT TX line samples before leasing the UART")
    args = parser.parse_args()
    sys.path.insert(0, args.oep_client)
    from oep_client.v0.__main__ import open_client
    from oep_client.v0 import codec
    from oep_client.v0.flash_image import Target
    from oep_client.v0.services import FixtureUart

    names = sorted(p.name for p in BASIC.iterdir() if (p / f"{p.name}.ino").exists()) if args.sketch == "all" else [args.sketch]
    client = open_client(args.port, 3.0)
    target = Target(client)
    uart_fn = client.find(*codec.DEF_FIXTURE_UART[:2])
    if uart_fn is None:
        raise SystemExit("probe offers no fixture.uart")
    uart_service = FixtureUart(client, uart_fn.function)
    results = []
    for name in names:
        print(f"== {name}")
        r = run_one(name, args, client, target, uart_service, print)
        results.append(r)
        print(f"   {r['verdict'].upper()}: {r.get('why', '')}")
    summary = {"port": args.port, "fqbn": args.fqbn, "results": results,
               "counts": {v: sum(1 for r in results if r["verdict"] == v) for v in ("pass", "fail", "skip")}}
    print(json.dumps(summary["counts"]))
    if args.result_json:
        pathlib.Path(args.result_json).write_text(json.dumps(summary, indent=1) + "\n")
    sys.exit(0 if summary["counts"]["fail"] == 0 else 1)


if __name__ == "__main__":
    main()
