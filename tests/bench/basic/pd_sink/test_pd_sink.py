"""The USB PD sink negotiates with a real charger.

The sketch (pd_sink.ino) runs one driver call per command; this side reads
what the charger offers and decides what to ask for, so the same test serves
any charger: every fixed level, each PPS range at its ends and middle (held
past the charger's 10 s PPS timeout on maintain() alone), requests nothing
offers, and a restart while the charger still holds the previous contract
(the case every re-flash hits: no Source_Capabilities arrive on their own).

Needs a PD charger on the board's USB-C: the bench file says so with
facts.pd_source; any other bench skips. Whether VBUS really follows the
contract is pd_vbus.py beside this file: a manual run with the VBUS meter rig
connected for it. Run with the bench's .env:

  uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \
    pytest bench/basic/pd_sink --profile ch32x035 -s
"""
import re

import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")

FIXED, PPS = 0, 1
STATE = re.compile(rb"(PD \w+) ok=(\d+) ready=(\d) connected=(\d) v=(\d+) a=(\d+) "
                   rb"idx=(-?\d+) pps=(\d) n=(\d+) ms=(\d+)")
PDO = re.compile(rb"PD PDO (\d+) kind=(\d+) min=(\d+) max=(\d+) ma=(\d+) raw=([0-9A-F]+)")


def cmd(dut, line: str, timeout: float = 10) -> dict:
    dut.write(line + "\n")
    m = dut.expect(STATE, timeout=timeout)
    keys = ("ok", "ready", "connected", "v", "a", "idx", "pps", "n", "ms")
    st = dict(zip(keys, (int(g) for g in m.groups()[1:])))
    print(f"{line:24s} -> {st}")
    return st


def profiles(dut) -> list[dict]:
    dut.write("PD LIST\n")
    out = []
    while True:
        m = dut.expect([PDO, re.compile(rb"PD LIST end n=(\d+)")], timeout=5)
        if m.group(0).startswith(b"PD LIST end"):
            assert int(m.group(1)) == len(out)
            return out
        i, kind, lo, hi, ma, raw = m.groups()
        out.append(dict(i=int(i), kind=int(kind), min=int(lo), max=int(hi), ma=int(ma), raw=raw.decode()))
        print(f"PDO {out[-1]}")


def contract(st: dict, mv: int, idx: int, pps: bool) -> None:
    assert st["ok"] == 1 and st["ready"] == 1, st
    assert st["v"] == mv and st["idx"] == idx and st["pps"] == int(pps), st


@pytest.fixture
def pd(dut, bench):
    if not bench.facts.get("pd_source"):
        pytest.skip(f"{bench.name} declares no facts.pd_source (no PD charger on the USB-C)")
    kit.start(dut, "pd_sink")
    # Re-flashed under a charger that already holds a contract: the driver has
    # to get Source_Capabilities back by itself (Soft_Reset after 620 ms).
    st = cmd(dut, "PD BEGIN")
    assert st["ok"] == 1 and st["connected"] == 1, st
    assert st["ready"] == 1, f"no contract within 3 s: {st}"
    assert st["ms"] < 2000, st
    contract(st, 5000, 0, False)
    pdos = profiles(dut)
    assert pdos and pdos[0]["kind"] == FIXED and pdos[0]["min"] == 5000, pdos
    yield pdos
    cmd(dut, "PD REQ 5000 0")


def test_pd_fixed(dut, pd):
    for p in [p for p in pd if p["kind"] == FIXED] + [pd[0]]:
        st = cmd(dut, f"PD REQ {p['min']} 0")
        contract(st, p["min"], p["i"], False)
        assert st["a"] == p["ma"], st
        assert st["ms"] < 1000, st


def test_pd_refused(dut, pd):
    def offered(mv):
        return any(p["min"] <= mv <= p["max"] for p in pd if p["kind"] in (FIXED, PPS))
    cmd(dut, "PD REQ 5000 0")
    for mv in (8000, 7000, 25000):
        if offered(mv):
            continue
        st = cmd(dut, f"PD REQ {mv} 0")
        assert st["ok"] == 0, st
        contract(dict(st, ok=1), 5000, 0, False)       # the 5 V contract stands
    st = cmd(dut, f"PD REQ 5000 {pd[0]['ma'] + 10}")   # more current than offered
    assert st["ok"] == 0, st
    contract(dict(st, ok=1), 5000, 0, False)
    st = cmd(dut, "PD REQ 9 0")                         # 9 mV, not 9 V
    assert st["ok"] == 0, st


def test_pd_pps(dut, pd):
    ranges = [p for p in pd if p["kind"] == PPS]
    if not ranges:
        pytest.skip("the charger offers no PPS range")
    for p in ranges:
        mid = (p["min"] + p["max"]) // 2 // 20 * 20
        for mv in (p["min"], mid, p["max"]):
            st = cmd(dut, f"PD PROF {p['i']} {mv} 1000")
            contract(st, mv, p["i"], True)
            assert st["a"] == 1000, st
        # Twice the charger's tPPSTimeout: only the driver's keepalive keeps it.
        st = cmd(dut, "PD HOLD 20000", timeout=30)
        contract(st, p["max"], p["i"], True)
        # 21 mV truncates to 20 mV steps; past the range is refused.
        st = cmd(dut, f"PD PROF {p['i']} {mid + 21} 0")
        contract(st, mid + 20, p["i"], True)
        st = cmd(dut, f"PD PROF {p['i']} {p['max'] + 20} 0")
        assert st["ok"] == 0 and st["v"] == mid + 20 and st["pps"] == 1, st
    st = cmd(dut, "PD REQ 5000 0")
    contract(st, 5000, 0, False)


def test_pd_restart(dut, pd):
    """end() and begin() again on a 9 V contract: the charger keeps it, the
    driver has to recover it with a Soft_Reset and settle on 5 V."""
    nine = [p for p in pd if p["kind"] == FIXED and p["min"] == 9000]
    if nine:
        contract(cmd(dut, "PD REQ 9000 0"), 9000, nine[0]["i"], False)
    st = cmd(dut, "PD END")
    assert st["ready"] == 0 and st["connected"] == 0 and st["idx"] == -1, st
    st = cmd(dut, "PD BEGIN")
    contract(st, 5000, 0, False)
    assert st["ms"] < 2000, st
