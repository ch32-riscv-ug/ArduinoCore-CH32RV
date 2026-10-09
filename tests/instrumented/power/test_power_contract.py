"""usb.port-cycle: disappearance, independent sibling survival and descriptor identity on return."""
import json
import pathlib
import subprocess
import time


def devices():
    root = pathlib.Path("/sys/bus/usb/devices")
    return {p.name for p in root.glob("*") if (p / "idVendor").exists()}


def test_power_contract(record_property):
    target = json.loads(pathlib.Path(__file__).with_name("case.json").read_text())["target"]
    topology = target["usb_topology"]
    hub = topology.rsplit(".", 1)[0]
    ports = ",".join(map(str, target["power_ports"]))
    affected = {f"{hub}.{p}" for p in target["power_ports"]}
    before = devices()
    assert topology in before
    old_number = (pathlib.Path("/sys/bus/usb/devices") / topology / "devnum").read_text().strip()
    try:
        off = subprocess.run(["uhubctl", "-l", hub, "-p", ports, "-a", "off"], capture_output=True, text=True, check=True)
        record_property("off_output", off.stdout + off.stderr)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and devices() & affected:
            time.sleep(0.1)
        assert not devices() & affected, "OFF left the target enumerated"
        assert before - affected <= devices(), "OFF affected a different USB device"
        time.sleep(2)
    finally:
        on = subprocess.run(["uhubctl", "-l", hub, "-p", ports, "-a", "on"], capture_output=True, text=True, check=True)
        record_property("on_output", on.stdout + on.stderr)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and topology not in devices():
        time.sleep(0.1)
    usb = pathlib.Path("/sys/bus/usb/devices") / topology
    assert topology in devices(), "probe did not re-enumerate"
    for key, file in [("usb_vid", "idVendor"), ("usb_pid", "idProduct"), ("usb_serial", "serial")]:
        if key in target:
            assert (usb / file).read_text().strip() == target[key]
    assert (usb / "devnum").read_text().strip() != old_number
