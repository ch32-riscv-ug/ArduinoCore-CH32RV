"""A bench target from its address alone (the user's rule, 2026-11-04: the tests take only the address that ch32rv
takes, CH32RV_PROBE in tests/.env; no bench file).

  oep://<unit>/<port>[/<target>]   an OEP probe's port (oep-spec host guide §5.3)
  wchlink://<serial>               a WCH-Link

Every fact comes from where it is true:
- the probe: its USB device (found by its serial, which is the OEP unit ID or the Link's serial) and its hub port,
  the lines and pads of its labels, its memo records (records.py: board, parts, where the DUT's USB goes, defects);
- the chip, over the probe (ch32rv target info): part or candidates, UID, chip ID; the part from oep.probe.memo/port's `part`
  only when the chip ID leaves several, and only one of them;
- this repository: fqbn and clock from boards.txt, which series have which peripherals (coverage.toml's needs),
  which parts have a USB host (CH32RVUsbHost);
- the bench at run time: the DUT's own USB device by its serial (CH32RVUsbDevice's default is the UID), the ESP32-S3
  peer by its port's USB serial (the record's).
Nothing is assumed: a fact that is not there leaves its cases out, with the reason."""
import json
import os
import pathlib
import re
import subprocess
import tomllib

from harness import records

REPO = pathlib.Path(__file__).resolve().parents[2]
SYSFS = pathlib.Path("/sys/bus/usb/devices")

# Which series run the USB device and host cases is coverage.toml's (usb-device, usb-peer-host); these are the parts
# of a host series without the host half or without USBFS (CH32RVUsbHostPort.h).
USB_DEVICE_ONLY_PARTS = ("CH32X035F8U6", "CH32X035F7P6", "CH32X035D8U6", "CH32X033F8P6")
USB_HOST_NO_PARTS = USB_DEVICE_ONLY_PARTS + ("CH32V203F6P6", "CH32V203G6U6")
UIAPDUINO_BOARD = "uiapduino:pro-micro-ch32v003-v1.4"
USB_CASES = ("usb", "usb-classes", "usb-media")
PEER_DEVICE_CASES = ("usb-peer", "usb-peer-classes", "usb-peer-media", "usb-peer-net", "usb-peer-fs")
PEER_HOST_CASES = ("usb-peer-host", "usb-peer-host-classes")
# Cases that need nothing but the chip and the probe (each test skips what its lines do not allow).
ALWAYS = ("runtime", "uart", "flash", "synckit", "logic", "gpio", "spi", "softspi", "wire", "softwire", "periph",
          "signals")
# A defect's `lose` names a feature; these are the cases that test it.
FEATURE_CASES = {"i2c": ("wire",), "spi": ("spi",), "usb": USB_CASES + PEER_DEVICE_CASES + PEER_HOST_CASES,
                 "uart": ("uart",), "sd": ("sd",), "pd": ("pd",), "eth": ("eth",)}


class PlaceError(ValueError):
    pass


def parse(address):
    """{'kind': 'oep', 'unit', 'port', 'target'} or {'kind': 'wchlink', 'serial'}."""
    m = re.fullmatch(r"oep://([0-9A-Fa-f]{12,32})/([A-Za-z][A-Za-z0-9-]{0,15})(?:/([A-Za-z0-9.-]+))?", address)
    if m:
        return dict(kind="oep", unit=m.group(1).lower(), port=m.group(2), target=(m.group(3) or "").lower() or None)
    m = re.fullmatch(r"wchlink://([0-9A-Za-z]{8,32})", address)
    if m:
        return dict(kind="wchlink", serial=m.group(1).upper())
    raise PlaceError(f"{address!r}: an address is oep://<unit>/<port>[/<target>] or wchlink://<serial> "
                     "(a probe without a port has no target)")


def usb_device(serial, sysfs=SYSFS):
    """The USB device whose serial is `serial` (case-insensitive): (topology, vid, pid, serial)."""
    hits = []
    for dev in sysfs.glob("*"):
        try:
            got = (dev / "serial").read_text().strip()
        except OSError:
            continue
        if got.lower() == serial.lower() and (dev / "idVendor").exists():
            hits.append((dev.name, (dev / "idVendor").read_text().strip(), (dev / "idProduct").read_text().strip(), got))
    if len(hits) != 1:
        raise PlaceError(f"USB serial {serial}: {'not connected' if not hits else 'on several devices: ' + str(hits)}")
    return hits[0]


def tty_of(topology, interface=None, sysfs=SYSFS):
    """The tty of a USB device (one of its interfaces), by its stable /dev/serial/by-id path when there is one."""
    found = []
    for tty in sorted(sysfs.glob(f"{topology}:*/tty*/*")) + sorted(sysfs.glob(f"{topology}:*/tty/*")):
        if interface is not None and not tty.parent.parent.name.endswith(f".{interface}"):
            continue
        found.append(tty.name)
    by_id = pathlib.Path("/dev/serial/by-id")
    for name in found:
        for link in sorted(by_id.glob("*")) if by_id.exists() else []:
            if link.resolve().name == name:
                return str(link)
    return f"/dev/{found[0]}" if found else None


def boards(path=REPO / "boards.txt"):
    """{part: (board id, the first clock entry's f_cpu)} from boards.txt's pnum menus."""
    text = path.read_text(encoding="utf-8")
    first_clock = {}
    for board, hz in re.findall(r"^([A-Za-z0-9_]+)\.menu\.clock\.[^.=\n]+\.build\.f_cpu=(\d+)L?$", text, re.M):
        first_clock.setdefault(board, int(hz))
    out = {}
    for board, part in re.findall(r"^([A-Za-z0-9_]+)\.menu\.pnum\.([A-Z0-9]+)=", text, re.M):
        out.setdefault(part, (board, first_clock.get(board)))
    return out


def fqbn_of(part):
    found = boards().get(part)
    if not found:
        raise PlaceError(f"{part}: no board in boards.txt names it (pnum)")
    board, f_cpu = found
    if not f_cpu:
        raise PlaceError(f"{board}: no clock menu in boards.txt")
    return f"ch32-riscv-ug:ch32rv:{board}:pnum={part}", f_cpu


def series_needs():
    """{case: [coverage items]} from tests/coverage.toml, and the series capabilities, for 'has the peripheral'."""
    from harness.series import series_capabilities
    items = tomllib.loads((REPO / "tests/coverage.toml").read_text())["item"]
    return items, {s: set(c) for s, c in series_capabilities().items()}


def _item_tests(item):
    test = item.get("test", [])
    return [test] if isinstance(test, str) else list(test)


def series_has(case, series, items=None, caps=None):
    """Whether the series has what the case tests (coverage.toml's needs / any_of / also / not_on); a case no item
    names is taken as every series'."""
    if items is None:
        items, caps = series_needs()
    mine = [i for i in items if any(t.partition("::")[0] == case for t in _item_tests(i))]
    if not mine:
        return True
    have = caps.get(series, set())
    for item in mine:
        if series in item.get("not_on", []):
            continue
        if series in item.get("also", []):
            return True
        need, any_of = set(item.get("needs", [])), set(item.get("any_of", []))
        if need <= have and (not any_of or any_of & have):
            return True
    return False


def series_of(part, caps):
    found = [s for s in caps if part.upper().startswith(s.upper())]
    return max(found, key=len) if found else None


def choose_cases(place, items=None, caps=None):
    """{case: 'run' | 'skip: <why>'} for the place."""
    if items is None:
        items, caps = series_needs()
    part, core, oep = place["chip"], place["record"], place["kind"] == "oep"
    series = series_of(part, caps)
    from harness.series import part_capabilities
    whole = caps
    caps = dict(caps, **{series: part_capabilities(series, part, whole)}) if series else caps
    out = {}
    for case in ALWAYS:
        out[case] = ("run" if series_has(case, series, items, caps) else
                     f"skip: {part} has no {case}" if series_has(case, series, items, whole) else
                     f"skip: {series} has no {case}")
    if oep and out.get("uart") == "run":
        lacking = [line for line in ("rx", "tx") if place["lines"].get(line) is None]
        if lacking:
            out["uart"] = "skip: the probe labels no " + " / ".join(f"{place.get('port_name') or '<port>'}.{l}" for l in lacking)
    parts = core.get("parts", {})
    out["sd"] = "run" if "sd" in parts else "skip: no sd in the record's parts"
    out["eth"] = ("run" if "eth" in parts and series_has("eth", series, items, caps)
                  else "skip: no eth in the record's parts" if "eth" not in parts else f"skip: {series} has no Ethernet")
    out["pd"] = ("run" if "pd" in core and series_has("pd", series, items, caps)
                 else "skip: no pd in the record" if "pd" not in core else f"skip: {series} has no USB PD")
    usb = core.get("usb", {})
    device = series_has("usb", series, items, caps)
    host = series_has("usb-peer-host", series, items, caps) and part not in USB_HOST_NO_PARTS
    for case in USB_CASES:
        out[case] = ("run" if usb.get("to") == "host" and device else
                     f"skip: {part} has no USB controller" if not device else
                     "skip: the record's usb is not {to: host}")
    for case in PEER_DEVICE_CASES:
        out[case] = ("run" if usb.get("to") == "esp32-s3" and device else
                     f"skip: {part} has no USB controller" if not device else
                     "skip: the record's usb is not {to: esp32-s3}")
    for case in PEER_HOST_CASES:
        out[case] = ("run" if usb.get("to") == "esp32-s3" and host else
                     f"skip: {part} has no USB host CH32RVUsbHost drives" if not host else
                     "skip: the record's usb is not {to: esp32-s3}")
    out["power"] = ("run" if oep and core.get("supply", {}).get("from") == "probe"
                    else "skip: the record's supply is not {from: probe}" if oep else "skip: not on an OEP port")
    out["hid"] = ("run" if core.get("board") == UIAPDUINO_BOARD and oep and place["lines"].get("nrst") is not None
                  else "skip: not a UIAPduino board with an nrst line")
    for defect in core.get("defects", []):
        for feature in defect.get("lose", []):
            for case in FEATURE_CASES.get(feature, ()):
                if out.get(case) == "run":
                    out[case] = f"skip: defect {defect.get('id')} ({feature})"
    return out


def probe_facts(address, parsed):
    """What the probe holds: labels, records, its own description (lock-free, no session)."""
    from oep_client import link, settings
    from harness.provenance import oep_snapshot
    host = link.open_host(f"oep://{parsed['unit']}", keep_session=False)
    try:
        labels = settings.labels(host)
        ports = settings.ports_named(labels)
        if parsed["port"].lower() not in {p.lower() for p in ports}:
            raise PlaceError(f"{address}: the probe has no port {parsed['port']} (its ports: {', '.join(ports) or 'none'})")
        port = next(p for p in ports if p.lower() == parsed["port"].lower())
        port_rec, core_rec, warnings = records.read(host, port)
        lines = {}
        pads = {}
        for channel, text in labels:
            got = settings.parse_label(text)
            if not got or got[0].lower() != port.lower():
                continue
            _, line, pad = got
            if line:
                lines[line.lower()] = channel
            if pad:
                pads[pad.lower()] = channel
    finally:
        host.link.close()
    snapshot = oep_snapshot(f"oep://{parsed['unit']}")
    return dict(port=port, labels=labels, lines=lines, pads=pads, port_record=port_rec, record=core_rec,
                warnings=warnings, snapshot=snapshot)


def chip_info(tool, address, part, log, env):
    command = [str(tool), "--probe", address, "--non-interactive", "--json", "target", "info"]
    if part:
        command[3:3] = ["--chip", part]
    log.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
    log.write_text(json.dumps(command) + "\n" + result.stdout + result.stderr)
    try:
        info = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise PlaceError(f"ch32rv target info gave no JSON (exit {result.returncode}): see {log}")
    if not info.get("ok"):
        raise PlaceError(f"ch32rv target info failed: {info.get('error') or info}; see {log}")
    return info.get("target") or {}


def resolve(address, tool, env, logs, *, chip=None):
    """The place of `address`: the facts above, as one dict (run_hardware.py stages it as case.json's target).
    chip: the part when the chip ID leaves several and no record names it (CH32RV_CHIP, ch32rv's own variable)."""
    parsed = parse(address)
    place = dict(address=address, kind=parsed["kind"], warnings=[])
    if parsed["kind"] == "oep":
        topology, vid, pid, serial = usb_device(parsed["unit"])
        facts = probe_facts(address, parsed)
        place.update(name=facts["port"].lower(), port_name=facts["port"], upload_port=address,
                     probe_unit_id=parsed["unit"], lines=facts["lines"], pads=facts["pads"],
                     record=facts["record"], port_record=facts["port_record"], snapshot=facts["snapshot"])
        place["warnings"] += facts["warnings"]
        place["port"] = address
        recorded_part = facts["port_record"].get("part")
    else:
        topology, vid, pid, serial = usb_device(parsed["serial"])
        place.update(name="link-" + parsed["serial"].lower(), lines={}, pads={}, record={}, port_record={},
                     snapshot=None, port=tty_of(topology))
        recorded_part = None
    place.update(usb_topology=topology, usb_vid=vid, usb_pid=pid, usb_serial=serial,
                 power_hub=topology.rsplit(".", 1)[0], power_ports=[int(topology.rsplit(".", 1)[1])])
    problems = records.check_core(place["record"])
    if problems:
        raise PlaceError(f"{address}: the core record has errors: " + "; ".join(problems))
    # The chip: ch32rv names the part, or the family with candidates; a record or CH32RV_CHIP picks one of those.
    info = chip_info(tool, address, None, logs / "info.log", env)
    part = info.get("sku")
    candidates = info.get("candidates") or []
    if not part:
        wanted = recorded_part or chip
        names = [c if isinstance(c, str) else c.get("sku") for c in candidates]
        if not wanted:
            raise PlaceError(f"{address}: the chip ID leaves {', '.join(names) or info.get('family')}: "
                             + ("write the part in the port record (tests/bench_record.py --part)" if parsed["kind"] == "oep"
                                else "set CH32RV_CHIP in tests/.env (a WCH-Link keeps no record)"))
        if names and wanted not in names:
            raise PlaceError(f"{address}: the part {wanted} is not one of the chip's {', '.join(names)}")
        info = chip_info(tool, address, wanted, logs / "info-part.log", env)
        part = info.get("sku") or wanted
    uid, family = info.get("uid", ""), info.get("family") or part[:8]
    place.update(chip=part, dut_uid=uid, dut_chip_id=info.get("chip_id"), family=family,
                 target_name=f"{family.lower()}-{uid.lower()}")
    expected = parsed.get("target") or place["port_record"].get("target")
    if expected and expected != place["target_name"]:
        where = "the address" if parsed.get("target") else "the port record"
        raise PlaceError(f"{address}: {where} names {expected}, the chip is {place['target_name']}: another board is on "
                         "the port (correct the record with tests/bench_record.py, or the address)")
    if not expected and parsed["kind"] == "oep":
        place["warnings"].append(f"no target in {address} or its port record: the board is not checked against one")
    place["fqbn"], place["f_cpu"] = fqbn_of(part)
    place["dut_usb_serial"] = uid.upper()
    core = place["record"]
    if core.get("usb", {}).get("to") == "esp32-s3":
        place["usb_peer_serial"] = core["usb"]["serial"]
    if "pd" in core:
        place.update({f"pd_{k}": v for k, v in core["pd"].items()})
        if "vbus_pin" in core["pd"]:
            place["pd_vbus_pin"] = pin_number(core["pd"]["vbus_pin"])
    if parsed["kind"] == "oep" and "nrst" in place["lines"]:
        place["nrst_channel"] = place["lines"]["nrst"]
    place["cases"] = choose_cases(place)
    return place


def pin_number(pad):
    """The sketch's pin number of a pad (pa7): the variants' CH32RV_PIN(port, bit)."""
    pad = pad.upper()
    return ((ord(pad[1]) - ord("A") + 2) << 5) | int(pad[2:])


def peer_tty(serial, sysfs=SYSFS):
    """The ESP32-S3 peer's tty, by the USB serial of its port to this host (records.py: usb.serial)."""
    topology = usb_device(serial, sysfs)[0]
    tty = tty_of(topology, sysfs=sysfs)
    if tty is None:
        raise PlaceError(f"ESP32-S3 {serial}: its USB device has no tty")
    return tty


def address_from_env(path=None):
    """CH32RV_PROBE from the environment or tests/.env (the only setting the tests take)."""
    value = os.environ.get("CH32RV_PROBE")
    env_file = pathlib.Path(path) if path else REPO / "tests/.env"
    if not value and env_file.exists():
        for line in env_file.read_text().splitlines():
            m = re.fullmatch(r"\s*CH32RV_PROBE\s*=\s*(\S+)\s*", line)
            if m:
                value = m.group(1).strip("'\"")
    return value
