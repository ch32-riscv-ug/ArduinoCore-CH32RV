"""The board records kept on an OEP probe's memo area (oep-spec interfaces/oep-if-memo §4), one pair per port.
The rules are tests/RECORDS.ja.md; this module checks them, and where the two differ the document is right:

  oep.probe.memo/port/<port>                   the project's port record (§4.3): target, part, note
  io.github.ch32-riscv-ug.arduino/port/<port>  this core's: what only these tests use (CORE_MEMBERS)

The port name goes in lower case (§4.1). Both are UTF-8 JSON objects; a reader skips members it does not know, and a
rewrite reads and writes inside one locked session, keeping them (§4.2). Nothing here is enforced by the probe, and
nothing that must be (a pad that must never be driven is the probe's setting, which the probe refuses itself).

What goes here is what differs between boards and cannot be read from the hardware: which board model is on the
port, what is wired to it besides the probe's lines, where its USB goes, its own defects. What the probe says (its
labels: the lines and the pads they reach) and what the chip says (part, UID) are not repeated, except the part when
the chip ID leaves several (oep.probe.memo/port's `part`), and what is the same for every board of a model or every chip of a
series is in the tests, not here. A record is a copy: the user keeps the right one on the host too."""
import json
import re

OWNER = "io.github.ch32-riscv-ug.arduino"
PORT_RE = re.compile(r"[A-Za-z][A-Za-z0-9-]{0,15}")
PAD_RE = re.compile(r"p[a-z][0-9]{1,2}")
HEX64_RE = re.compile(r"[0-9a-f]{16}")


def port_record_name(port):
    return f"oep.probe.memo/port/{port.lower()}"


def core_record_name(port):
    return f"{OWNER}/port/{port.lower()}"


def _pads(value, where, errors):
    for key, pad in value.items():
        if key in ("bus", "to", "kind", "note"):
            continue
        if not isinstance(pad, str) or not PAD_RE.fullmatch(pad):
            errors.append(f"{where}.{key}: a DUT pad in lower case (pa5), not {pad!r}")


def _sd(value, errors):
    bus = value.get("bus")
    need = {"spi": ("sck", "miso", "mosi", "cs"), "sd1": ("clk", "cmd", "dat0"),
            "sd4": ("clk", "cmd", "dat0", "dat1", "dat2", "dat3")}.get(bus)
    if need is None:
        errors.append(f"parts.sd.bus: spi, sd1 or sd4, not {bus!r}")
        return
    missing = [k for k in need if k not in value]
    if missing:
        errors.append(f"parts.sd ({bus}) lacks {', '.join(missing)}")
    _pads(value, "parts.sd", errors)


def _eth(value, errors):
    if value.get("to") != "lan":
        errors.append("parts.eth.to: lan (the board's Ethernet jack is on the host's network)")


PART_CHECKS = {"sd": _sd, "eth": _eth}


def _parts(value, errors):
    if not isinstance(value, dict):
        errors.append("parts: an object of part: {...}")
        return
    for kind, part in value.items():
        if kind in PART_CHECKS:
            if not isinstance(part, dict):
                errors.append(f"parts.{kind}: an object")
            else:
                PART_CHECKS[kind](part, errors)


def _usb(value, errors):
    to = value.get("to")
    if to == "host":
        return
    if to in ("esp32-s3", "esp32-p4"):
        # serial finds the port on any host; a bridge without one (a CH340) is found by mac alone, through the
        # bench's board-identify names (/run/board-identify/by-id/<chip>-<mac>).
        if "serial" in value and (not isinstance(value["serial"], str) or not value["serial"]):
            errors.append(f"usb.serial: the USB serial of the {to.upper()}'s own port to this host (its USB-UART "
                          "bridge's or its USB-Serial/JTAG's), which finds it on any host")
        if "serial" not in value and "mac" not in value:
            errors.append(f"usb: the {to.upper()}'s serial (its port's USB serial) or, for a bridge without one, mac")
        if "mac" in value and (not isinstance(value["mac"], str) or not re.fullmatch(r"[0-9a-f]{12}", value["mac"])):
            errors.append(f"usb.mac: the {to.upper()}'s MAC, 12 lower-case hex digits")
        return
    if to == "rp2040":
        if not isinstance(value.get("port"), str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9-]{0,15}", value["port"]):
            errors.append("usb.port: the RP2040's port on the same OEP probe (its SWD and RTT console), e.g. pico")
        return
    errors.append(f"usb.to: host, esp32-s3, esp32-p4 or rp2040, not {to!r}")


def _pd(value, errors):
    if "max_mv" in value and (type(value["max_mv"]) is not int or not 5000 <= value["max_mv"] <= 48000):
        errors.append("pd.max_mv: the highest input voltage the board takes, 5000-48000")
    if ("vbus_pin" in value) != ("vbus_ratio" in value):
        errors.append("pd.vbus_pin and pd.vbus_ratio go together")
    if "vbus_pin" in value and not PAD_RE.fullmatch(str(value["vbus_pin"])):
        errors.append("pd.vbus_pin: a DUT pad in lower case")


def _defects(value, errors):
    if not isinstance(value, list):
        errors.append("defects: a list")
        return
    for n, d in enumerate(value):
        if not isinstance(d, dict) or not isinstance(d.get("id"), str) or not isinstance(d.get("lose"), list):
            errors.append(f"defects[{n}]: {{id, lose: [feature, ...], why}}")


def _board(value, errors):
    if not isinstance(value, str) or not re.fullmatch(r"[a-z0-9-]+:[A-Za-z0-9._-]+", value):
        errors.append(f"board: <maker>:<model> (wch:CH32X035C8T-R0), not {value!r}")


def _supply(value, errors):
    if value.get("from") not in ("probe", "usb", "other"):
        errors.append("supply.from: probe (the probe's USB port powers the board), usb (its own USB) or other")


# member: (checker, what it is)
CORE_MEMBERS = {
    "board": (_board, "the board model, <maker>:<model>; the tests know each model's onboard parts"),
    "supply": (_supply, "where the board's power comes from: {from: probe | usb | other}"),
    "usb": (_usb, "where the DUT's own USB goes: {to: host}, {to: esp32-s3 / esp32-p4, serial and / or mac} or {to: rp2040, port}"),
    "parts": (_parts, "external parts: {sd: {bus: spi | sd1 | sd4, <signal>: <pad>, ...}, eth: {to: lan}}"),
    "pd": (_pd, "a USB PD sink on the board: {max_mv, vbus_pin, vbus_ratio}"),
    "defects": (_defects, "this board's own faults: [{id, lose: [feature, ...], why}]"),
    "note": (lambda v, e: None, "for people"),
    "updated": (lambda v, e: None if re.fullmatch(r"\d{4}-\d{2}-\d{2}", str(v)) else e.append("updated: YYYY-MM-DD"),
                "the day it was last written"),
}


def check_core(record):
    """Problems of a core record (unknown members are other tools' and are fine)."""
    errors = []
    if not isinstance(record, dict):
        return ["the record is not a JSON object"]
    for key, (checker, _) in CORE_MEMBERS.items():
        if key in record:
            checker(record[key], errors)
    return errors


def check_port(record):
    errors = []
    if not isinstance(record, dict):
        return ["the record is not a JSON object"]
    if "target" in record and not re.fullmatch(r"[a-z0-9]+-[0-9a-f]+", str(record["target"])):
        errors.append("target: <series>-<UID hex> in lower case (ch32x035-1ff9abcd880ebc48)")
    if "part" in record and not re.fullmatch(r"CH32[A-Z0-9]+", str(record["part"])):
        errors.append("part: the part number as the maker writes it (CH32X035C8T6)")
    return errors


def target_name(family, uid):
    """host guide §5.3: <series>-<the individual's value in hex>, lower case."""
    return f"{family.lower()}-{uid.lower()}"


def _absent(error):
    """A memo read of a name the probe does not have: rejected no_resource (oep-if-memo §2.2)."""
    from oep_client import host as h, message as m
    return isinstance(error, h.Rejected) and error.result.resolution == m.REJECTED and error.result.detail == m.NO_RESOURCE


def _get(mm, name):
    from oep_client import host as h
    try:
        return mm.read(name)
    except h.Rejected as error:
        if _absent(error):
            return None
        raise


def _decode(data):
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def read(host, port):
    """(port record, core record, warnings) of the port; an absent, torn or unreadable object is {} with a warning
    (a torn one also when it is there: the memo's crc does not match, oep-if-memo §3)."""
    from oep_client import memo
    warnings = []
    try:
        mm = memo.Memo(host)
    except LookupError as error:   # no oep.probe.memo in the probe's list
        return {}, {}, [f"no memo area on the probe ({error})"]
    out = []
    for name in (port_record_name(port), core_record_name(port)):
        try:
            data = _get(mm, name)
        except memo.MemoTorn:
            warnings.append(f"{name}: torn (its crc does not match), read as absent; write it again")
            data = None
        value = {} if data is None else _decode(data)
        if value is None:
            warnings.append(f"{name}: not a JSON object, read as absent")
            value = {}
        out.append(value)
    return out[0], out[1], warnings


def encode(record):
    data = json.dumps(record, ensure_ascii=False, indent=None, separators=(",", ":"), sort_keys=False).encode("utf-8")
    return data


def update(host, port, *, port_changes=None, core_changes=None, remove=()):
    """Merge changes into the port's records inside the session the caller holds (the memo write needs the lock, so
    no other host writes between this read and write, oep-if-memo §4.2): read, set the given members (None removes
    one), keep the rest, write only what changed, and never empty a record (refused). Returns {name: (before, after)} of
    what was written."""
    from oep_client import memo
    mm = memo.Memo(host)
    written = {}
    for name, changes, check in ((port_record_name(port), port_changes, check_port),
                                 (core_record_name(port), core_changes, check_core)):
        if changes is None and not remove:
            continue
        try:
            data = _get(mm, name)
        except memo.MemoTorn:
            data = None     # torn: written again whole below
        before = (_decode(data) or {}) if data is not None else {}
        after = dict(before)
        for key, value in (changes or {}).items():
            if value is None:
                after.pop(key, None)
            else:
                after[key] = value
        for key in remove:
            if name == core_record_name(port):
                after.pop(key, None)
        problems = check(after)
        if problems:
            raise ValueError(f"{name}: " + "; ".join(problems))
        if after == before:
            continue
        data = encode(after)
        if len(data) > mm.max_object:
            raise ValueError(f"{name}: {len(data)} bytes, the probe keeps at most {mm.max_object}")
        if not after:
            # Never empties a record: a probe's v208 and m030 records were found gone on 2026-11-12 with no tool but
            # this one able to delete a memo object - to drop a whole record, delete it on the probe on purpose.
            raise ValueError(f"{name}: the change would leave the record empty; refusing to delete it")
        mm.write(name, data)
        written[name] = (before, after)
    return written
