"""Host-local bench settings and identity checks; importing this never opens hardware."""
import pathlib
import re
import tomllib
from contextlib import contextmanager

CASES = {"runtime", "uart", "gpio", "hid", "power"}
TARGET_KEYS = {
    "port", "chip", "fqbn", "f_cpu", "usb_topology", "usb_vid", "usb_pid",
    "usb_serial", "dut_uid", "dut_chip_id", "cases", "power_ports", "gpio",
    "soft_usb_topology", "nrst_channel", "upload_port", "probe_unit_id",
}


def load_bench(path, *, lock_path=None):
    path = pathlib.Path(path).resolve()
    data = tomllib.loads(path.read_text())
    if set(data) != {"lock_file", "targets"}:
        raise ValueError("bench needs only lock_file and targets")
    if lock_path is not None:
        data["lock_file"] = str(pathlib.Path(lock_path).resolve())
    if not isinstance(data["lock_file"], str) or not data["lock_file"]:
        raise ValueError("an explicit shared lock_file is required")
    lock = pathlib.Path(data["lock_file"])
    if not lock.is_absolute():
        lock = path.parent / lock
    data["lock_file"] = str(lock.resolve())
    if not isinstance(data["targets"], dict) or not data["targets"]:
        raise ValueError("bench needs at least one target")
    ports, topologies = set(), set()
    for name, target in data["targets"].items():
        if not re.fullmatch(r"[a-z][a-z0-9-]*", name):
            raise ValueError(f"invalid target name: {name}")
        if not isinstance(target, dict) or set(target) - TARGET_KEYS:
            raise ValueError(f"unknown target fields: {name}")
        required = {"port", "chip", "fqbn", "f_cpu", "usb_topology", "usb_vid", "usb_pid", "cases"}
        if not required <= target.keys():
            raise ValueError(f"missing target fields: {name}")
        if not isinstance(target["port"], str) or not target["port"].startswith("/dev/serial/by-id/") and not target["port"].startswith("/run/board-identify/by-id/"):
            raise ValueError("port must be an explicit stable serial path")
        if not re.fullmatch(r"\d+-\d+(?:\.\d+)+", target["usb_topology"]):
            raise ValueError("usb_topology must name a downstream USB device")
        if any(not re.fullmatch(r"[0-9a-f]{4}", target[k]) for k in ("usb_vid", "usb_pid")):
            raise ValueError("USB IDs must be four lowercase hexadecimal digits")
        if not isinstance(target["f_cpu"], int) or target["f_cpu"] <= 0:
            raise ValueError("f_cpu must be a positive integer")
        if not target["fqbn"].startswith("ch32-riscv-ug:ch32rv:"):
            raise ValueError("only the worktree CH32 core is supported")
        if not isinstance(target["cases"], list) or not target["cases"] or set(target["cases"]) - CASES:
            raise ValueError("unknown or empty case list")
        if len(set(target["cases"])) != len(target["cases"]):
            raise ValueError("duplicate case")
        if target["port"] in ports or target["usb_topology"] in topologies:
            raise ValueError("duplicate physical target")
        ports.add(target["port"])
        topologies.add(target["usb_topology"])
        if not target.get("dut_uid") and not target.get("dut_chip_id"):
            raise ValueError("expected DUT UID or chip ID is required")
        if "probe_unit_id" in target and not re.fullmatch(r"[0-9a-fA-F]{12,32}", target["probe_unit_id"]):
            raise ValueError("probe_unit_id must be a complete OEP hexadecimal unit ID")
        if (target["usb_vid"], target["usb_pid"]) != ("1a86", "8010"):
            if not target.get("probe_unit_id") and not ((target["usb_vid"], target["usb_pid"]) == ("1209", "4f45") and target.get("usb_serial")):
                raise ValueError("OEP target needs an expected probe unit ID")
        if "soft_usb_topology" in target and not re.fullmatch(r"\d+-\d+(?:\.\d+)+", target["soft_usb_topology"]):
            raise ValueError("invalid soft USB topology")
        if "nrst_channel" in target and (type(target["nrst_channel"]) is not int or not 0 <= target["nrst_channel"] <= 65535):
            raise ValueError("invalid NRST channel")
        if "upload_port" in target:
            prefix = "oep://" + target.get("usb_serial", "") + "/"
            hid = (target["upload_port"] == "hid://" + target.get("soft_usb_topology", "")
                   and target.get("soft_usb_topology") and target["chip"] == "CH32V003F4U6"
                   and target["fqbn"].endswith(":UIAPDUINO_V003_V14") and "nrst_channel" in target)
            oep = (target.get("usb_serial") and target["upload_port"].startswith(prefix)
                   and re.fullmatch(r"[a-zA-Z0-9_-]+", target["upload_port"][len(prefix):]))
            if not hid and not oep:
                raise ValueError("upload_port must name this target's OEP slot or UIAPduino HID topology")
        if "power" in target["cases"]:
            power_ports = target.get("power_ports")
            if not isinstance(power_ports, list) or not power_ports or any(type(p) is not int or p < 1 for p in power_ports):
                raise ValueError("power needs an explicit list of ports")
            if len(set(power_ports)) != len(power_ports) or int(target["usb_topology"].rsplit(".", 1)[1]) not in power_ports:
                raise ValueError("power_ports must include the probe exactly once")
            own_hub = target["usb_topology"].rsplit(".", 1)[0]
            allowed = {int(target["usb_topology"].rsplit(".", 1)[1])}
            if target.get("soft_usb_topology"):
                if target["soft_usb_topology"].rsplit(".", 1)[0] != own_hub:
                    raise ValueError("probe and soft USB must share a hub")
                allowed.add(int(target["soft_usb_topology"].rsplit(".", 1)[1]))
            if set(power_ports) != allowed:
                raise ValueError("power_ports must name only this target's USB connections")
        if "gpio" in target["cases"]:
            gpio = target.get("gpio")
            if not isinstance(gpio, list) or not gpio:
                raise ValueError("gpio requires explicit pad/channel wiring")
            channels, pads = set(), set()
            for pin in gpio:
                if set(pin) != {"pad", "channel"} or not re.fullmatch(r"P[A-F](?:[0-9]|[12][0-9]|30)", pin["pad"]):
                    raise ValueError("invalid GPIO pad/channel mapping")
                if type(pin["channel"]) is not int or not 0 <= pin["channel"] <= 65535:
                    raise ValueError("invalid GPIO channel")
                if pin["pad"] in pads or pin["channel"] in channels:
                    raise ValueError("duplicate GPIO pad/channel")
                if pin["pad"] in {"PD1", "PD7", "PD3", "PD4", "PD5", "PD6"}:
                    raise ValueError("GPIO test must not drive debug/reset/USB/UART pads")
                pads.add(pin["pad"])
                channels.add(pin["channel"])
        if "hid" in target["cases"]:
            if target["chip"] != "CH32V003F4U6" or not target.get("soft_usb_topology") or type(target.get("nrst_channel")) is not int:
                raise ValueError("hid needs UIAPduino, soft USB topology and NRST wiring")
    return data


@contextmanager
def shared_lock(path):
    """Use the existing shared inode throughout hardware operation; never create a private lock."""
    import fcntl
    with pathlib.Path(path).open("r+") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def check_probe(target, snapshot):
    if (target["usb_vid"], target["usb_pid"]) == ("1a86", "8010"):
        return
    expected = target.get("probe_unit_id") or target.get("usb_serial")
    if not expected or (snapshot.get("unit_id") or "").lower() != expected.lower():
        raise ValueError("OEP probe unit ID mismatch or unavailable; refusing target operations")


def check_identity(target, *, sysfs=pathlib.Path("/sys/bus/usb/devices"), tty_root=pathlib.Path("/sys/class/tty")):
    """Confirm stable path, USB topology and descriptor identity before opening the probe."""
    usb = sysfs / target["usb_topology"]
    for key, filename in (("usb_vid", "idVendor"), ("usb_pid", "idProduct"), ("usb_serial", "serial")):
        if key in target and (usb / filename).read_text().strip() != target[key]:
            raise ValueError(f"USB identity mismatch: {key}")
    port = pathlib.Path(target["port"]).resolve(strict=True)
    device = (tty_root / port.name / "device").resolve(strict=True)
    if usb.resolve() not in device.parents:
        raise ValueError("serial port is on a different USB topology")


def check_target(target, info):
    result = info.get("target") or info.get("result", {}).get("target", {})
    if not info.get("ok") or result.get("sku") != target["chip"]:
        raise ValueError("DUT SKU does not match the selected profile")
    if "dut_uid" in target and result.get("uid") != target["dut_uid"]:
        raise ValueError("DUT UID mismatch")
    if "dut_chip_id" in target and result.get("chip_id", "").lower() != target["dut_chip_id"].lower():
        raise ValueError("DUT chip ID mismatch")
    return result
