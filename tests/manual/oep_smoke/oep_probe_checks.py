# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""The OEP probe's own debug parts on a jig, independent of any sketch's tests: reset-halt stops before the first
instruction, step moves the PC, a DMI delay step takes its time, and - where the probe labels a channel NRST - attach under
reset stops at the vector and the reset-line search finds that channel and no other.

    uv run tests/manual/oep_smoke/oep_probe_checks.py --target x035|v003|l103 [--json out.json]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import targets  # noqa: E402

DEFAULT_CLIENT = HERE.parents[4] / "dev_oep" / "oep-client-python" / "src"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--target", required=True, choices=sorted(k for k, v in targets.TARGETS.items() if "wire" in v))
    ap.add_argument("--oep-client", default=str(DEFAULT_CLIENT))
    ap.add_argument("--json")
    args = ap.parse_args()
    sys.path.insert(0, args.oep_client)
    from oep_client.v1 import host, link, target

    prof = targets.TARGETS[args.target]
    hst = host.Host(link.SerialLink(prof["port"]).send)
    hst.open(lease_ms=30000)
    wire = target.Wire(hst, prof["wire"])
    nrst = target.probe_labels(hst).get("NRST")
    checks: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        checks.append((name, ok, detail))
        print(f"  {'PASS' if ok else 'FAIL'} {name} {detail}")

    try:
        conn, _ = wire.attach(halt=True)
        dm = target.RiscvDm(hst, conn)
        dpcs = [dm.reset_halt() for _ in range(10)]
        check("reset_halt stops at the vector", all(d == 0 for d in dpcs), f"dpc {sorted({hex(d) for d in dpcs})}")
        steps = [dm.step() for _ in range(5)]
        check("step moves the pc", all(moved for moved, _, _ in steps), f"{steps[0][1]:#x} -> {steps[-1][2]:#x}")
        t0 = time.perf_counter()
        dm.dmi(dm.step_delay(20000) + dm.step_read(0x11))
        ms = (time.perf_counter() - t0) * 1000
        check("dmi delay 20 ms", 20 <= ms < 200, f"{ms:.1f} ms round trip")
        flags, attempts, pc = dm.reset(confirm=True)
        check("reset and run", flags & 2 != 0, f"flags {flags:#x} attempts {attempts} pc {pc:#x}")
        wire.detach(conn)
        if nrst is None:
            print("  skip attach under reset: the probe labels no NRST channel")
        else:
            got = []
            for _ in range(5):
                conn, dpc = wire.attach_under_reset()
                got.append(dpc)
                try:
                    target.RiscvDm(hst, conn).resume()
                except host.Rejected:
                    pass
                wire.detach(conn)
            check("attach under reset stops at the vector", got.count(0) >= 4, f"dpc {[hex(d) for d in got]}")
            candidates = sorted(set(prof.get("gpio", {}).values()) | {nrst})
            hits = wire.find_reset_line(candidates)
            check("reset-line search finds NRST only", hits == [nrst], f"hits {hits} of {len(candidates)} candidates")
        conn, _ = wire.attach(halt=False)
        target.RiscvDm(hst, conn).reset(confirm=True)
        wire.detach(conn)
    finally:
        hst.end()
    passed = sum(ok for _, ok, _ in checks)
    summary = {"target": args.target, "pass": passed, "fail": len(checks) - passed,
               "checks": [{"name": n, "ok": ok, "detail": d} for n, ok, d in checks]}
    print(json.dumps({k: summary[k] for k in ("pass", "fail")}))
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(summary, indent=2))
    return 0 if passed == len(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
