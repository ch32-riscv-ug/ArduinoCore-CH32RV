"""VBUS follows the PD contract - checked with the VBUS meter rig.

Manual (no test_ prefix: `pytest bench` never collects it). The rig
(tests/manual/vbus_meter/README.ja.md) is connected across VBUS for this run
only; test_pd_sink.py beside this file is the automatic test and needs no rig.
For every fixed profile and each PPS range's ends and middle, the meter's
reading has to be within 10 % of the contract - a rough "VBUS really moved",
not a measurement of the charger: how close it gets is its business.

  cd tests
  TEST_VBUS_METER=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \\
    uv run --env-file .env --with pytest-embedded-arduino-cli-ch32rv \\
    pytest bench/basic/pd_sink/pd_vbus.py --profile ch32x035 -s
"""
import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")
pds = load("tests/bench/basic/pd_sink/test_pd_sink.py", "pd_sink_kit")
vm = load("tests/manual/vbus_meter/vbus_meter.py", "vbus_meter")

FLOOR_MV = 4500


def check(meter, mv: int) -> None:
    """Within 10 % from 4.5 V up. Below, only that VBUS came down from 5 V: the
    PPS charger on the WeAct bench advertises 3.3 V but stops near 4 V (3.98 V
    on the rig and on an inline USB tester, 2026-10-02)."""
    got = meter.millivolts()
    print(f"VBUS {mv} mV contract -> {got} mV ({got / mv:.3f})")
    if mv >= FLOOR_MV:
        assert abs(got - mv) <= mv * 0.10, f"VBUS {got} mV on a {mv} mV contract"
    else:
        assert got < FLOOR_MV, f"VBUS {got} mV on a {mv} mV contract"


def test_vbus_follows_contract(dut, bench):
    if not bench.facts.get("pd_source"):
        pytest.skip(f"{bench.name} declares no facts.pd_source")
    meter = vm.open_meter()
    if meter is None:
        pytest.skip(f"{vm.ENV} is not set (the VBUS meter rig is not connected)")
    try:
        kit.start(dut, "pd_sink")
        pds.contract(pds.cmd(dut, "PD BEGIN"), 5000, 0, False)
        profiles = pds.profiles(dut)
        check(meter, 5000)
        for p in profiles:
            if p["kind"] == pds.FIXED:
                pds.contract(pds.cmd(dut, f"PD REQ {p['min']} 0"), p["min"], p["i"], False)
                check(meter, p["min"])
            elif p["kind"] == pds.PPS:
                mid = (p["min"] + p["max"]) // 2 // 20 * 20
                for mv in (p["min"], mid, p["max"]):
                    pds.contract(pds.cmd(dut, f"PD PROF {p['i']} {mv} 1000"), mv, p["i"], True)
                    check(meter, mv)
        pds.contract(pds.cmd(dut, "PD REQ 5000 0"), 5000, 0, False)
        check(meter, 5000)
    finally:
        meter.close()
