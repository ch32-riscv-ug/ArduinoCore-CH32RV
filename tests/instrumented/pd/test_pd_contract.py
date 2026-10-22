"""pd.attach / pd.caps / pd.fixed / pd.pps / pd.restart: CH32RVUsbPd as a sink
(tests/sketch_support/pd/pd.ino) against the USB PD charger on the DUT's USB-C.

Bench settings on the target:
  pd_max_mv      the highest voltage the board's input takes (default 5000: only
                 the charger's 5 V contract and refusals are tried)
  pd_vbus_pin    optional: an analog pin on VBUS through a divider, and
  pd_vbus_ratio  that divider's VBUS / pin ratio; with both, every contract is
                 also measured (within 10 %)

Every fixed level up to pd_max_mv and back to 5 V; a PPS profile (if offered)
at its lowest point and its middle, the middle held 12 s on maintain() alone;
a voltage nothing offers is refused and leaves the contract alone; end() and
begin() under a live contract get a contract back."""
import json
import pathlib
import re
import secrets
import time


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


STATE = rb"STATE (\d) (\d) (\d+) (\d+) (-?\d+) (\d)"
CONTRACT = rb" (\d) (\d+) (\d+) (-?\d+)"
FIXED, PPS = 0, 1


def test_pd_contract(dut, record_property):
    target = settings(__file__)["target"]
    max_mv = int(target.get("pd_max_mv", 5000))
    vbus_pin, ratio = target.get("pd_vbus_pin"), target.get("pd_vbus_ratio")
    nonce = secrets.token_hex(4)
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d) (-?\d+) (-?\d+)", timeout=15)
    assert m.group(1).decode() == settings(__file__)["build_id"], "the DUT runs another build"
    assert m.group(2) == b"1", "USBPD.begin() failed: no USBPD block on this part"
    record_property("cc_pins", f"{m.group(3).decode()} {m.group(4).decode()}")

    def measure(mv):
        if vbus_pin is None or ratio is None:
            return
        time.sleep(0.3)
        got = int(ask(dut, f"vbus {vbus_pin}", rb"VBUS (\d+)").group(1)) * float(ratio)
        record_property(f"vbus_at_{mv}", round(got))
        assert abs(got - mv) <= mv * 0.10, f"VBUS {got:.0f} mV under a {mv} mV contract"

    # pd.attach: a contract (the driver's own 5 V Request) within 5 s
    m = ask(dut, "wait 5000", STATE, timeout=10)
    assert m.group(1) == b"1", "no source on CC"
    assert m.group(2) == b"1", "a source without USB PD (or no contract)"
    assert int(m.group(3)) == 5000 and m.group(5) == b"0"
    measure(5000)

    # pd.caps
    count = int(ask(dut, "caps", rb"CAPS (\d+)").group(1))
    profiles = []
    for _ in range(count):
        m = dut.expect(rb"PDO (\d+) (\d+) (\d+) (\d+) (\d+)\r?\n", timeout=5)
        profiles.append(tuple(int(g) for g in m.groups()))
    record_property("profiles", " ".join(f"{k}:{lo}-{hi}/{ma}" for _, k, lo, hi, ma in profiles))
    assert count >= 1 and profiles[0][1:4] == (FIXED, 5000, 5000), "profile 0 must be fixed 5 V"

    # pd.fixed: every level the board takes, then back to 5 V
    for i, kind, lo, hi, ma in profiles:
        if kind != FIXED or hi > max_mv or hi == 5000:
            continue
        m = ask(dut, f"req {hi}", rb"REQ" + CONTRACT, timeout=10)
        assert m.groups() == (b"1", str(hi).encode(), str(ma).encode(), str(i).encode()), m.group(0)
        measure(hi)
    m = ask(dut, "req 5000", rb"REQ" + CONTRACT, timeout=10)
    assert m.group(1) == b"1" and int(m.group(2)) == 5000

    # a level nothing offers: refused, the contract stands
    offered = {(lo, hi) for _, k, lo, hi, _ in profiles if k in (FIXED, PPS)}
    odd = next(mv for mv in range(5100, 21000, 100) if not any(lo <= mv <= hi for lo, hi in offered))
    m = ask(dut, f"req {odd}", rb"REQ" + CONTRACT, timeout=10)
    assert m.group(1) == b"0" and int(m.group(2)) == 5000, f"{odd} mV: {m.group(0)}"

    # pd.pps: the first PPS profile the board takes
    pps = next(((i, lo, hi) for i, k, lo, hi, _ in profiles if k == PPS and lo <= max_mv), None)
    if pps is None:
        record_property("pps", "none offered within pd_max_mv")
    else:
        i, lo, hi = pps
        mid = min((lo + hi) // 2, max_mv) // 20 * 20
        for mv in (lo, mid):
            m = ask(dut, f"reqp {i} {mv}", rb"REQP" + CONTRACT, timeout=10)
            assert m.group(1) == b"1" and int(m.group(2)) == mv and int(m.group(4)) == i, m.group(0)
            measure(mv)
        time.sleep(12)            # the source drops PPS after 15 s without a Request
        m = ask(dut, "state", STATE)
        assert (m.group(2), int(m.group(3)), m.group(6)) == (b"1", mid, b"1"), "the PPS contract did not hold"
        measure(mid)
        assert ask(dut, "req 5000", rb"REQ" + CONTRACT, timeout=10).group(1) == b"1"

    # pd.restart: end() and begin() under a live contract
    assert ask(dut, "restart", rb"RESTART (\d)").group(1) == b"1"
    m = ask(dut, "wait 6000", STATE, timeout=12)
    assert m.group(2) == b"1" and int(m.group(3)) == 5000, "no contract after a restart"
