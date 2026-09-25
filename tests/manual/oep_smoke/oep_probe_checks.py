# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""The OEP probe's own debug parts on a jig, independent of any sketch's tests: reset-halt stops before the first
instruction, step moves the PC, a DMI delay step takes its time, and - where the probe labels a channel NRST - attach under
reset stops at the vector and the reset-line search finds that channel and no other. Also the v1 session rules
(oep-spec v1-open-proposals §3 / §4): a request sent again with the same corr is answered from the probe's table, the
connection number moves on, a lapsed lease releases the plan and an explicit end keeps it.

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
    hst = link.open_host(prof["port"])
    hst.open(lease_ms=30000)
    wire = target.Wire(hst, prof["wire"])
    nrst = target.probe_labels(hst).get("NRST")
    checks: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str = "") -> None:
        checks.append((name, ok, detail))
        print(f"  {'PASS' if ok else 'FAIL'} {name} {detail}")

    def guarded(name, fn):
        """One check; an exception is that check's failure (with where it came from), and the run goes on."""
        try:
            fn()
        except Exception as e:   # noqa: BLE001 - a probe check reports, it does not stop the run
            check(name, False, f"{type(e).__name__}: {e}")

    def debug_ops():
        conn, _ = wire.attach(halt=True)
        dm = target.RiscvDm(hst, conn)
        try:
            dpcs = [dm.reset_halt() for _ in range(10)]
            check("reset_halt stops at the vector", all(d == 0 for d in dpcs), f"dpc {sorted({hex(d) for d in dpcs})}")
            steps = [dm.step() for _ in range(5)]
            check("step moves the pc", all(moved for moved, _, _ in steps), f"{steps[0][1]:#x} -> {steps[-1][2]:#x}")

            def round_trip(us):
                t0 = time.perf_counter()
                dm.dmi(dm.step_delay(us) + dm.step_read(0x11))
                return (time.perf_counter() - t0) * 1000
            base, ms = min(round_trip(0) for _ in range(3)), min(round_trip(20000) for _ in range(3))
            # 10 % slack: the host's own clock has been seen running several percent slow (WSL, 2026-09-24)
            check("dmi delay 20 ms", 18 <= ms - base < 200, f"{ms - base:.1f} ms more than a 0 ms delay")
            flags, attempts, pc = dm.reset(confirm=True)
            check("reset and run", flags & 2 != 0, f"flags {flags:#x} attempts {attempts} pc {pc:#x}")
        finally:
            wire.detach(conn)

    def under_reset():
        got = []
        for _ in range(5):
            conn, dpc = wire.attach_under_reset()
            got.append(dpc)
            try:
                target.RiscvDm(hst, conn).resume()
            except host.OepError:
                pass   # a CH32L103 raises no allresumeack
            wire.detach(conn)
        check("attach under reset stops at the vector", got.count(0) >= 4, f"dpc {[hex(d) for d in got]}")

    def reset_line():
        candidates = sorted(set(prof.get("gpio", {}).values()) | {nrst})
        hits = wire.find_reset_line(candidates)
        check("reset-line search finds NRST only", hits == [nrst], f"hits {hits} of {len(candidates)} candidates")

    def session_rules():
        import struct
        from oep_client.v1 import core, message as m, riscv
        gpio = target.find_all(hst, "oep.fixture.gpio")
        line = sorted(prof.get("gpio", {}).values())
        role = core.TAG_ROLE_ASSIGNMENT

        def raw(corr, fn, op, payload):   # one request with an explicit corr, no re-send
            return m.Result.unpack(hst.link.send(m.Request(corr, fn, op, payload, session=hst.session).pack()))

        # the connection number moves on; an old number is no_connection
        c1, _ = wire.attach(halt=False)
        wire.detach(c1)
        c2, _ = wire.attach(halt=False)
        try:
            wire.detach(c1)
            old_refused = False
        except host.OepError as e:
            old_refused = "connection" in str(e).lower()
        check("a new connection takes the next number, the old one is refused", c2 == c1 % 255 + 1 and old_refused,
              f"{c1} -> {c2}")
        # a request sent again with the same corr is answered from the table, not run twice
        if gpio and line:
            body = bytes([role, 5]) + struct.pack("<HBH", gpio[0], 1, line[0])
            corr = hst.next_corr()
            first, again = raw(corr, 0, core.OP_PLAN_APPLY, body), raw(corr, 0, core.OP_PLAN_APPLY, body)
            other = raw(corr, 0, core.OP_PLAN_APPLY, bytes([role, 5]) + struct.pack("<HBH", gpio[0], 1, line[-1]))
            check("a repeat is answered from the table", first.succeeded and again.succeeded and again.detail == first.detail,
                  f"first {first.resolution}/{first.detail} again {again.resolution}/{again.detail}")
            check("the same corr with another payload is corr_reused",
                  other.resolution == m.REJECTED and other.detail == m.CORR_REUSED, f"{other.resolution}/{other.detail}")
            # an explicit end keeps the plan (the next session gets it); a lapse releases it
            hst.end()
            hst.open(lease_ms=1000)
            kept = raw(hst.next_corr(), 0, core.OP_PLAN_APPLY, body)
            check("an explicit end keeps the plan", kept.resolution == m.REJECTED and kept.detail == m.UNAVAILABLE,
                  f"{kept.resolution}/{kept.detail}")
            time.sleep(1.6)
            hst.lock_state()                                   # any request lets the probe notice the lapse
            hst.open(lease_ms=30000)
            after = raw(hst.next_corr(), 0, core.OP_PLAN_APPLY, body)
            check("a lapsed lease released the plan", after.succeeded, f"{after.resolution}/{after.detail}")
            core.plan_release(hst)
            try:
                wire.detach(c2)
                dropped = False
            except host.OepError as e:
                dropped = "connection" in str(e).lower()
            check("a lapsed lease released the host's connection", dropped)
        # a result too large to keep comes back result_lost when sent again
        c2, _ = wire.attach(halt=True)
        dm = riscv.RiscvDm(hst, c2)
        dm.halt()
        corr = hst.next_corr()
        body = dm.prefix + struct.pack("<IH", prof.get("ram_base", 0x20000000), 64)   # the connection first
        big, again = raw(corr, dm.fn, riscv.RiscvDm.READ_BLOCK, body), raw(corr, dm.fn, riscv.RiscvDm.READ_BLOCK, body)
        check("a repeat of a large result is result_lost",
              big.succeeded and again.resolution == m.REJECTED and again.detail == m.RESULT_LOST,
              f"{len(big.payload)} bytes, again {again.resolution}/{again.detail}")
        dm.reset(confirm=True)
        wire.detach(c2)

    try:
        guarded("session rules", session_rules)
        guarded("debug operations", debug_ops)
        if nrst is None:
            print("  skip attach under reset: the probe labels no NRST channel")
        else:
            guarded("attach under reset", under_reset)
            guarded("reset-line search", reset_line)
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
