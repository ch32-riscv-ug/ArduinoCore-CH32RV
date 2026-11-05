"""usb.port-cycle: the hub reports the ports off, the siblings survive, and the probe re-enumerates (a new device
number) with the same descriptors."""
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
        # The hub's own word that the ports are off. Whether the kernel sees the device go now depends on the path:
        # when uhubctl may not write the port's sysfs `disable` it falls back to libusb, and the kernel learns of the
        # disconnect only when the port is powered again (seen on 1a86:8094, 2026-11-05: disconnect and reconnect in
        # the same instant, a new device number). The re-enumeration below is the proof the power went.
        status = off.stdout.split("New status", 1)[-1]
        for port in target["power_ports"]:
            line = next((l for l in status.splitlines() if l.strip().startswith(f"Port {port}:")), "")
            assert " off" in line, f"the hub did not report port {port} off: {line.strip() or 'no status'}"
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and devices() & affected:
            time.sleep(0.1)
        record_property("gone_while_off", not devices() & affected)
        assert before - affected <= devices(), "OFF affected a different USB device"
        time.sleep(2)
    finally:
        on = subprocess.run(["uhubctl", "-l", hub, "-p", ports, "-a", "on"], capture_output=True, text=True, check=True)
        record_property("on_output", on.stdout + on.stderr)
    usb = pathlib.Path("/sys/bus/usb/devices") / topology

    def number():
        try:
            return (usb / "devnum").read_text().strip()
        except OSError:
            return None
    # Until the new device is there: the old one can still be listed for a moment after ON.
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and number() in (None, old_number):
        time.sleep(0.1)
    time.sleep(0.5)    # its attributes are written after the directory appears
    assert topology in devices(), "probe did not re-enumerate"
    for key, file in [("usb_vid", "idVendor"), ("usb_pid", "idProduct"), ("usb_serial", "serial")]:
        if key in target:
            assert (usb / file).read_text().strip() == target[key]
    assert (usb / "devnum").read_text().strip() != old_number, "the probe kept its device number: the power did not go"
