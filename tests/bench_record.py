#!/usr/bin/env python3
"""Show or write a bench port's records on its OEP probe (tests/harness/records.py).

  uv run --project tests --locked --extra hardware python tests/bench_record.py show [ADDRESS]
  uv run --project tests --locked --extra hardware python tests/bench_record.py set [ADDRESS] --target auto \\
      --board wch:CH32X035C8T-R0 --supply probe --usb esp32-s3,serial=58FA041040,mac=e4b063b49f2c \\
      --sd '{"bus": "sd1", "clk": "pa5", "cmd": "pa7", "dat0": "pa6"}' \\
      --defect '{"id": "x035-adc-ch-i2c-unavailable", "lose": ["i2c"], "why": "..."}'
  uv run ... python tests/bench_record.py plan [ADDRESS]      # what a run would do there, without running it

ADDRESS defaults to CH32RV_PROBE (tests/.env). `set` reads the records and writes them back inside one session (the
memo write needs the lock), changing only the members it is given and keeping the rest; `--remove MEMBER` drops a
member of the core record. `--target auto` reads the chip (ch32rv target info) and records its name in oep.probe.memo/port/<port>.
Keep the right records on the host too: the probe's are a copy that travels with it."""
import argparse
import datetime
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from harness import place, records  # noqa: E402


def open_host(parsed):
    from oep_client import link
    return link.open_host(f"oep://{parsed['unit']}")


def port_of(host, parsed):
    from oep_client import settings
    ports = settings.ports_named(settings.labels(host))
    for p in ports:
        if p.lower() == parsed["port"].lower():
            return p
    raise SystemExit(f"the probe has no port {parsed['port']} (its ports: {', '.join(ports) or 'none'})")


def show(address):
    parsed = place.parse(address)
    if parsed["kind"] != "oep":
        raise SystemExit("a WCH-Link keeps no records")
    host = open_host(parsed)
    try:
        port = port_of(host, parsed)
        port_rec, core_rec, warnings = records.read(host, port)
    finally:
        host.link.close()
    print(json.dumps({records.port_record_name(port): port_rec, records.core_record_name(port): core_rec},
                     indent=2, ensure_ascii=False))
    for w in warnings:
        print("warning:", w, file=sys.stderr)


def tool():
    sys.path.insert(0, str(place.REPO / "tests"))
    from harness.workspace import tool_paths
    return tool_paths()[0]


def set_records(address, args):
    parsed = place.parse(address)
    if parsed["kind"] != "oep":
        raise SystemExit("a WCH-Link keeps no records")
    port_changes, core_changes = {}, {}
    if args.target == "auto":
        info = place.chip_info(tool(), address, args.part, pathlib.Path(tempfile.mkdtemp()) / "info.log", None)
        # A chip without a chip ID (CH32V103) names no family: the given part does.
        family = info.get("family") or (info.get("sku") or args.part or "")[:8]
        port_changes["target"] = records.target_name(family, info["uid"])
    elif args.target:
        port_changes["target"] = args.target.lower()
    if args.part:
        port_changes["part"] = args.part
    if args.port_note:
        port_changes["note"] = args.port_note
    if args.board:
        core_changes["board"] = args.board
    if args.supply:
        core_changes["supply"] = {"from": args.supply}
    if args.usb:
        to, *rest = args.usb.split(",")
        usb = {"to": to}
        for item in rest:
            key, _, value = item.partition("=")
            usb[key] = value.lower().replace(":", "") if key == "mac" else value
        core_changes["usb"] = usb
    if args.sd or args.eth:
        parts = {}
        if args.sd:
            parts["sd"] = json.loads(args.sd)
        if args.eth:
            parts["eth"] = {"to": "lan"}
        core_changes["parts"] = parts
    if args.pd:
        core_changes["pd"] = json.loads(args.pd)
    if args.defect:
        core_changes["defects"] = [json.loads(d) for d in args.defect]
    if args.note:
        core_changes["note"] = args.note
    if core_changes or args.remove:
        core_changes["updated"] = datetime.date.today().isoformat()
    from oep_client import core
    host = open_host(parsed)
    try:
        port = port_of(host, parsed)
        core.take(host, 10000, owner="ArduinoCore bench_record", wait_s=30)
        try:
            written = records.update(host, port, port_changes=port_changes or None, core_changes=core_changes or None,
                                     remove=args.remove or ())
        finally:
            host.end()
    finally:
        host.link.close()
    for name, (before, after) in written.items():
        print(f"{name}: {len(records.encode(after))} bytes")
        print(json.dumps(after, indent=2, ensure_ascii=False))
    if not written:
        print("unchanged")


def plan(address):
    work = pathlib.Path(tempfile.mkdtemp())
    p = place.resolve(address, tool(), None, work, chip=None)
    keep = {k: v for k, v in p.items() if k not in ("snapshot",)}
    print(json.dumps(keep, indent=2, ensure_ascii=False))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("show", "set", "plan"):
        s = sub.add_parser(name)
        s.add_argument("address", nargs="?", default=None)
        if name == "set":
            s.add_argument("--target", help="auto (read the chip) or <series>-<uid>")
            s.add_argument("--part", help="the part, when the chip ID leaves several (oep.probe.memo/port's part)")
            s.add_argument("--port-note", help="oep.probe.memo/port's note")
            s.add_argument("--board", help="<maker>:<model>")
            s.add_argument("--supply", choices=("probe", "usb", "other"))
            s.add_argument("--usb", help="host, esp32-s3 / esp32-p4,serial=<its port's USB serial>,mac=<mac> (either may be left out; mac alone for a bridge without a serial), or rp2040,port=<port>")
            s.add_argument("--sd", help="JSON: {bus, <signal>: <pad>}")
            s.add_argument("--eth", action="store_true", help="the Ethernet jack is on the host's network")
            s.add_argument("--pd", help="JSON: {max_mv, vbus_pin, vbus_ratio}")
            s.add_argument("--defect", action="append", help="JSON: {id, lose: [...], why}; repeat for each")
            s.add_argument("--note")
            s.add_argument("--remove", action="append", help="a core record member to drop")
    args = ap.parse_args(argv)
    address = args.address or place.address_from_env()
    if not address:
        ap.error("no ADDRESS and no CH32RV_PROBE in the environment or tests/.env")
    if args.cmd == "show":
        show(address)
    elif args.cmd == "set":
        set_records(address, args)
    else:
        plan(address)


if __name__ == "__main__":
    main()
