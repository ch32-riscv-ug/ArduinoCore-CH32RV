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
from smoke import expectations, sketchbook, uses_uart   # noqa: E402  (pure helpers)
from targets import TARGETS                              # noqa: E402

DEFAULT_PORT = "/run/board-identify/by-id/esp32-series-30eda0e31108"
DEFAULT_CLIENT = REPO.parents[1] / "dev_oep" / "oep-client-python" / "src"


def toolchain_bin() -> pathlib.Path:
    candidates = sorted((REPO / ".tools" / "xpack-riscv-none-elf-gcc").glob("*/bin"))
    if not candidates:
        raise SystemExit("no xPack RISC-V toolchain under .tools/")
    return candidates[-1]


def build(name: str, fqbn: str, tmp: pathlib.Path, log,
          source: pathlib.Path | None = None, defines: list[str] | None = None) -> pathlib.Path:
    """Compile tests/sketches/basic/<name>, or `source` when another sketch dir is given.
    `defines` are extra -D flags (targets.build_defines) so one sketch can carry several pin tables."""
    sketch_dir = stage_sketch(source or BASIC / name, tmp / name)
    out = tmp / "build"
    cmd = ["arduino-cli", "compile", "--fqbn", fqbn,
           "--build-property", f"compiler.path={toolchain_bin()}/",
           *(["--build-property", f"build.extra_flags={' '.join(defines)}"] if defines else []),
           "--build-path", str(out), str(sketch_dir)]
    r = subprocess.run(cmd, capture_output=True, text=True, env=sketchbook(tmp))
    if r.returncode:
        raise RuntimeError("compile failed\n" + r.stdout + r.stderr)
    log("   " + r.stdout.strip().splitlines()[0])
    bins = list(out.glob("*.ino.bin"))
    if len(bins) != 1:
        raise RuntimeError(f"expected one .ino.bin, found {bins}")
    return bins[0]


class Link:
    """expect() over one of the probe's streams - target.console or fixture.uart, anything
    with read(n) and write(bytes) - polling it and keeping a cursor like smoke.py's Link."""

    def __init__(self, stream):
        self.uart = stream
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


def open_console(client) -> "Link":
    """The sketch's Console, through the probe's target.console (tests/sketches/testcmd.h).

    Needs no pins and no lease, so it stays open across whatever the caller leases and
    releases around it. A few tries: the probe's first attach after a reset can miss.
    """
    from oep_client.v0 import codec
    from oep_client.v0.services import TargetConsole
    fn = client.find(*codec.DEF_TARGET_CONSOLE[:2])
    if fn is None:
        raise SystemExit("probe offers no target.console; reflash it")
    console = TargetConsole(client, fn.function)
    for attempt in range(4):
        try:
            console.configure(True, TargetConsole.DMDATA)
            break
        except Exception:
            if attempt == 3:
                raise
            time.sleep(0.3)
    return Link(console)


def sync(link: "Link", banner: str, seconds: float = 10.0) -> bool:
    """Wait for the banner, then prove the link with PING <token> -> PONG <token>.

    The first exchange after programming or a probe-driven reset is the unreliable one: the
    reset check reads registers through abstract commands, which use the console's own DATA0,
    and a sketch already running reads that as input. A line break first isolates the noise
    as a line of its own; a token proves the answer is to this question and not a leftover;
    three tries absorb the one that gets eaten.
    """
    if not link.wait(banner, seconds):
        return False
    link.send("\n")
    for _ in range(3):
        token = random.randrange(1000, 9999)
        link.send(f"PING {token}\n")
        if link.wait(f"PONG {token}", 5):
            return True
    return False


def judge(name: str, script: tuple, text: str, missed: list) -> dict:
    if missed:
        return {"verdict": "fail", "why": f"never arrived: {missed}"}
    if "FAIL" in text:
        return {"verdict": "fail", "why": f"the sketch reported {[ln for ln in text.splitlines() if 'FAIL' in ln]}"}
    counts = re.findall(r"failures=(\d+)", text)
    if counts and any(c != "0" for c in counts):
        return {"verdict": "fail", "why": f"reported failures={counts}"}
    replayed = sum(1 for _, verb, _ in script if verb != "write")
    if not replayed and not counts:
        return {"verdict": "skip", "why": "nothing to check against"}
    return {"verdict": "pass", "why": ", ".join(p for p in (f"{replayed} expectations" if replayed else "",
                                                          f"failures={counts[-1]}" if counts else "") if p)}


def name_uart(console: Link, profile: dict, baud: int, log) -> bool:
    """Tell the sketch which UART to bring up, and wait for it to say it did."""
    n, route = profile["uart"]
    console.send(f"UART {n} {route} {baud}\n")
    if console.wait(f"UART OK {n} {route}", 5):
        return True
    log(f"   the sketch did not accept UART {n} {route} {baud}")
    return False


def run_one(name: str, args, profile: dict, client, target, uart_service, log) -> dict:
    from oep_client.v0.flash_image import program_image
    script = expectations(name)
    result = {"sketch": name}
    with tempfile.TemporaryDirectory() as tmp:
        t0 = time.perf_counter()
        try:
            binary = build(name, profile["fqbn"], pathlib.Path(tmp), log)
        except RuntimeError as e:
            log(str(e))
            return {**result, "verdict": "fail", "why": "compile failed"}
        image = binary.read_bytes()
        result["image_bytes"] = len(image)
        result["sha256"] = hashlib.sha256(image).hexdigest()
        result["compile_s"] = round(time.perf_counter() - t0, 3)
        t0 = time.perf_counter()
        # A few tries: a CH32L103 whose debug link sat idle (the probe rebooting, say) takes a
        # couple of attaches to come back, and the first halt of the preflight is where that
        # shows. Anything that is still refusing after that is a real failure.
        for attempt in range(5):
            try:
                outcome = program_image(target, image)   # preflight, diff, pages, CRC verify, reset
                break
            except Exception as e:
                if attempt == 4:
                    log(f"   program failed: {e}")
                    return {**result, "verdict": "fail", "why": f"program failed: {e}"}
                if attempt == 2:
                    # Still refusing: pull the target's NRST where the jig wires it, which
                    # starts it clean the way power-up does. Rejected (and harmless) on a
                    # probe with no reset line.
                    try:
                        ok, _ = target.control.reset_report(3)
                        if ok:
                            log("   attach kept failing; pulsed NRST")
                    except Exception:
                        pass
                time.sleep(0.3 * (attempt + 1))
        result["program"] = outcome.as_dict()
        result["program_s"] = round(time.perf_counter() - t0, 3)
        if not outcome.verified:
            return {**result, "verdict": "fail", "why": "program/verify failed"}
        log(f"   programmed {outcome.pages_changed} pages, verified, {result['program_s']} s")

    console = open_console(client)
    links = {"console": console}
    lease = None
    try:
        banner = f"{name} READY"
        if not sync(console, banner, args.seconds + 10):
            console.drain(0.5)
            log(f"   received {len(console.text)} chars: {console.text[:300]!r}")
            why = f"no '{banner}'" if banner not in console.text else "banner but no PONG"
            return {**result, "verdict": "fail", "why": why, "output": console.text}
        if uses_uart(script):
            if not profile.get("uart"):
                return {**result, "verdict": "skip", "why": "this jig has no UART the probe can reach"}
            lease, _ = client.plan_apply(uart_service.assignments(rx=profile["uart_rx"], tx=profile["uart_tx"]))
            uart_service.configure(args.baud)
            links["uart"] = Link(uart_service)
            if not name_uart(console, profile, args.baud, log):
                return {**result, "verdict": "fail", "why": "UART not accepted", "output": console.text}
        missed = []
        if not any(verb == "write" for _, verb, _ in script):
            console.send("RUN\n")
        last_write = None
        for stream, verb, text in script:
            link = links[stream]
            # The banner coming back after REBOOT / BITE is a real reset (watchdog timeouts run
            # seconds on a 128 kHz LSI); the pytest gives it 20 s, so does this replay.
            wait_s = max(args.seconds, 20.0) if text == banner else args.seconds
            if verb == "write":
                last_write = (link, text if text.endswith("\n") else text + "\n")
                link.send(last_write[1])
                continue
            found = link.wait(text, wait_s, regex=(verb == "match"))
            if not found and last_write and last_write[1].strip() not in ("REBOOT", "BITE"):
                # A command sent while the DUT was still starving its watchdog (the pre-reset READY
                # arrives before the reset does) is dropped; repeat it once, with the reset's budget.
                last_write[0].send(last_write[1])
                found = link.wait(text, 20.0, regex=(verb == "match"))
            if not found:
                missed.append(text)
        console.wait(f"{name} done failures=", args.seconds)
        console.drain(0.3)
        text = console.text + ("\n--- uart ---\n" + links["uart"].text if "uart" in links else "")
        verdict = judge(name, script, text, missed)
        return {**result, **verdict, "output": text}
    finally:
        if lease is not None:
            client.plan_release(lease)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=sorted(TARGETS), default="x035", help="jig profile (targets.py)")
    parser.add_argument("--port", default=os.environ.get("OEP_PROBE_PORT"), help="override the profile's probe port")
    parser.add_argument("--oep-client", default=str(DEFAULT_CLIENT))
    parser.add_argument("--sketch", default="core_api", help="case name under tests/sketches/basic, or 'all'")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=4.0)
    parser.add_argument("--result-json")
    args = parser.parse_args()
    sys.path.insert(0, args.oep_client)
    from oep_client.v0.__main__ import open_client
    from oep_client.v0 import codec
    from oep_client.v0.flash_image import Target
    from oep_client.v0.services import FixtureUart, TargetConsole

    profile = TARGETS[args.target]
    port = args.port or profile["port"]
    names = sorted(p.name for p in BASIC.iterdir() if (p / f"{p.name}.ino").exists()) if args.sketch == "all" else [args.sketch]
    client = open_client(port, 3.0)
    target = Target(client)
    uart_fn = client.find(*codec.DEF_FIXTURE_UART[:2])
    uart_service = FixtureUart(client, uart_fn.function) if uart_fn else None
    results = []
    for name in names:
        print(f"== {name}")
        r = run_one(name, args, profile, client, target, uart_service, print)
        results.append(r)
        print(f"   {r['verdict'].upper()}: {r.get('why', '')}")
    summary = {"target": args.target, "port": port, "fqbn": profile["fqbn"], "results": results,
               "counts": {v: sum(1 for r in results if r["verdict"] == v) for v in ("pass", "fail", "skip")}}
    print(json.dumps(summary["counts"]))
    if args.result_json:
        pathlib.Path(args.result_json).write_text(json.dumps(summary, indent=1) + "\n")
    sys.exit(0 if summary["counts"]["fail"] == 0 else 1)


if __name__ == "__main__":
    main()
