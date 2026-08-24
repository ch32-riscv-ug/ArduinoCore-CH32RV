#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Put one WCH-Link on the WSL side of USB/IP, and take the previous one off.

    uv run tests/manual/probe_switch/probe_switch.py                 # what is there
    uv run tests/manual/probe_switch/probe_switch.py V103            # switch to it
    uv run tests/manual/probe_switch/probe_switch.py 434A            # or by serial
    uv run tests/manual/probe_switch/probe_switch.py --detach        # bench idle

Only on a WSL host whose probes arrive over usbipd-win. Everywhere else the
probes are simply plugged in and this script has nothing to do; it says so and
exits rather than pretending.

--- why a switcher exists ------------------------------------------------------

WSL's vhci_hcd offers **eight high-speed ports** (plus eight super-speed ones
that no USB 2.0 adapter can use), and every serial adapter and probe on a bench
is USB 2.0. When they are full, `usbipd attach` fails with

    WSL usbip: error: no free port

which reads like a broken probe and is not one. On the bench this was written
for, seven of the eight were another project's ESP32 boards, so exactly one
WCH-Link could be attached at a time. The limit is the port count, not anything
about WCH-Link: free a port and two probes attach happily. The module exposes no
parameters (`modinfo vhci_hcd`), so eight is eight.

Attaching several is in any case not what makes a bench usable, because every
tool here already needs to be told *which* probe to talk to (CH32_PROBE). One
attached probe means one unambiguous answer, and switching costs about three
seconds.

--- serials, not COM numbers, not bus ids --------------------------------------

Three names exist for the same probe and only one of them is safe:

    bus id      16-2       renumbers when anything is replugged
    COM19       Windows    persists per device, but follows the *physical port*
    434A12...   USB serial burned into the probe; the only one that travels

So this resolves everything by USB serial, and re-reads the bus id from
`usbipd state` on every call rather than remembering one. COM numbers are
printed because they are what the Windows Device Manager shows a human, and for
no other reason.

Serials are unreadable, so give them names in tests/.env:

    CH32_PROBE_V103=434A124C5596
    CH32_PROBE_X035=FC928F068181

and then `probe_switch.py V103`. The names are yours - the script only reads the
CH32_PROBE_ prefix - and .env is not committed, so this bench's serials stay on
this bench. Run it with `uv run --env-file tests/.env ...` for the names to be
visible.

--- what it will not do --------------------------------------------------------

It detaches WCH-Links (1a86:8010 in RISC-V mode, 1a86:8012 in ARM mode) and
nothing else, ever. The other devices holding ports belong to whoever plugged
them in; when a port is needed the script says which devices are holding them
and stops.
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "manual" / "smoke"))

WCH_LINK_VID = 0x1A86
WCH_LINK_PIDS = (0x8010, 0x8012)
# The same devices as seen from Windows, where the InstanceId carries the serial.
INSTANCE_ID = re.compile(r"VID_1A86&PID_(8010|8012)\\(\w+)", re.I)

USBIPD_CANDIDATES = (
    "/mnt/c/Program Files/usbipd-win/usbipd.exe",
    "/mnt/c/Program Files (x86)/usbipd-win/usbipd.exe",
)


class Failure(Exception):
    pass


# --- the Windows side ---------------------------------------------------------

def usbipd() -> str:
    """Path to usbipd.exe, or a Failure explaining that this is not that host."""
    override = os.environ.get("CH32_USBIPD")
    if override:
        if not pathlib.Path(override).exists():
            raise Failure(f"CH32_USBIPD={override} does not exist")
        return override
    for path in USBIPD_CANDIDATES:
        if pathlib.Path(path).exists():
            return path
    found = subprocess.run(["which", "usbipd.exe"], capture_output=True, text=True)
    if found.returncode == 0 and found.stdout.strip():
        return found.stdout.strip()
    raise Failure(
        "usbipd.exe not found, so there is nothing to switch. This script is "
        "only for a WSL host whose probes arrive over usbipd-win; elsewhere, "
        "plug the probe in. Set CH32_USBIPD if it is installed somewhere else.")


def _state(exe: str) -> list:
    done = subprocess.run([exe, "state"], capture_output=True, text=True)
    if done.returncode != 0:
        raise Failure(f"usbipd state failed: {done.stderr.strip() or done.stdout.strip()}")
    return json.loads(done.stdout)["Devices"]


def windows_probes(exe: str) -> list:
    """Every WCH-Link Windows knows about, connected or merely remembered.

    Windows remembers a device it has seen before, so `busid is None` is the
    normal way a probe that is unplugged - or plugged into a hub that is off -
    shows up. That is worth printing rather than hiding: "I cannot find COM23"
    and "COM23 is not plugged in" are different problems.
    """
    rows = []
    for device in _state(exe):
        match = INSTANCE_ID.search(device["InstanceId"])
        if not match:
            continue
        com = re.search(r"\(COM(\d+)\)", device["Description"] or "")
        rows.append({
            "serial": match.group(2).upper(),
            "com": f"COM{com.group(1)}" if com else None,
            "busid": device["BusId"],
            "attached": device["ClientIPAddress"] is not None,
        })
    return sorted(rows, key=lambda r: (r["com"] is None, r["com"] or "", r["serial"]))


def port_holders(exe: str) -> list:
    """What is currently occupying the USB/IP ports, WCH-Links included."""
    return [f"{d['BusId']}  {d['Description']}"
            for d in _state(exe) if d["ClientIPAddress"] is not None]


# --- the Linux side -----------------------------------------------------------

def here() -> list:
    """[(serial, tty)] for the WCH-Links WSL can actually see, via smoke.py.

    Deliberately the same function the rest of the manual tests use: if this
    script and smoke.py ever disagreed about what is attached, the switch would
    look like it worked and the next command would not find the probe.
    """
    from smoke import find_probes
    return find_probes()


def wait_for(serial: str, seconds: float = 15.0):
    """Poll until the probe enumerates on the Linux side, or give up.

    Attach returns as soon as Windows has handed the device over; the CDC
    interface takes a moment more to bind, and a caller that runs probe-rs
    immediately gets "no probe found" from a bench that is perfectly fine.
    """
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for found, tty in here():
            if found.upper() == serial.upper():
                return tty
        time.sleep(0.4)
    return None


# --- names --------------------------------------------------------------------

def nicknames() -> dict:
    """{NAME: serial} from CH32_PROBE_<NAME> in the environment."""
    names = {}
    for key, value in os.environ.items():
        if key.startswith("CH32_PROBE_") and value.strip():
            names[key[len("CH32_PROBE_"):].upper()] = value.strip().upper()
    return names


def resolve(want: str, probes: list) -> dict:
    """A nickname, a whole serial, or any prefix of one - to exactly one probe."""
    want = want.strip().upper()
    serial = nicknames().get(want, want)

    hits = [p for p in probes if p["serial"] == serial]
    if not hits:
        hits = [p for p in probes if p["serial"].startswith(serial)]
    if not hits:
        known = ", ".join(f"{p['serial']} ({p['com'] or 'no COM'})" for p in probes)
        extra = ""
        if want != serial:
            extra = f" (CH32_PROBE_{want} names {serial})"
        raise Failure(f"no WCH-Link matching {want!r}{extra}. Known to Windows: "
                      f"{known or 'none'}")
    if len(hits) > 1:
        raise Failure(f"{want!r} matches {len(hits)} probes: "
                      + ", ".join(p["serial"] for p in hits))
    return hits[0]


def label(probe: dict, names: dict) -> str:
    """'V103' when .env names it, otherwise the empty string."""
    for name, serial in sorted(names.items()):
        if serial == probe["serial"]:
            return name
    return ""


# --- actions ------------------------------------------------------------------

def detach_all(exe: str, probes: list, keep: str = None) -> list:
    """Detach every attached WCH-Link except `keep`. Never touches anything else."""
    gone = []
    for probe in probes:
        if not probe["attached"] or not probe["busid"]:
            continue
        if keep and probe["serial"] == keep.upper():
            continue
        done = subprocess.run([exe, "detach", "--busid", probe["busid"]],
                              capture_output=True, text=True)
        if done.returncode != 0:
            raise Failure(f"detach {probe['busid']} ({probe['serial']}) failed: "
                          f"{done.stderr.strip() or done.stdout.strip()}")
        gone.append(probe)
    return gone


def attach(exe: str, probe: dict):
    """Attach one probe, translating the two failures that actually happen."""
    if not probe["busid"]:
        raise Failure(f"{probe['serial']} ({probe['com'] or 'no COM'}) is known to "
                      f"Windows but not plugged in right now")
    done = subprocess.run([exe, "attach", "--wsl", "--busid", probe["busid"]],
                          capture_output=True, text=True)
    if done.returncode == 0:
        return
    message = (done.stderr + done.stdout)
    if "no free port" in message:
        raise Failure(
            "all eight USB/IP high-speed ports are in use, so nothing more can "
            "attach. Free one on the Windows side - these are holding them:\n  "
            + "\n  ".join(port_holders(exe))
            + "\n(this script only ever detaches WCH-Links, so the rest are "
              "yours to decide about)")
    raise Failure(f"attach {probe['busid']} ({probe['serial']}) failed: "
                  + "\n".join(line for line in message.splitlines()
                              if line.strip() and not line.startswith("usbipd: info")))


def restore(exe: str, gone: list) -> None:
    """Re-attach what a failed switch had already detached, best effort.

    Bus ids are re-read rather than reused: detaching is exactly the kind of
    event that renumbers them.
    """
    if not gone:
        return
    current = {p["serial"]: p for p in windows_probes(exe)}
    for probe in gone:
        back = current.get(probe["serial"])
        if not back or not back["busid"]:
            print(f"could not put {probe['serial']} back: it is no longer "
                  f"plugged into Windows", file=sys.stderr)
            continue
        done = subprocess.run([exe, "attach", "--wsl", "--busid", back["busid"]],
                              capture_output=True, text=True)
        state = "back" if done.returncode == 0 else "NOT back - attach it by hand"
        print(f"{probe['serial']} {state}", file=sys.stderr)


def identify(serial: str, attempts: int = 3, gap: float = 1.5) -> list:
    """chip_info's report for the probe just attached, or a reason it is absent.

    Retried, because the first question after an attach loses a race often
    enough to matter: the CDC interface binds before the probe's vendor
    interface is ready, so wait_for() is satisfied while probe-rs still gets
    nothing and reports the target as unreachable. Asking again a second later
    answers correctly, and a bench that really has no target answers the same
    way every time - so retrying costs nothing but the truth.
    """
    sys.path.insert(0, str(REPO / "tests" / "manual" / "chip_info"))
    try:
        import chip_info
        from smoke import find_probe_rs
    except ImportError as e:                        # pragma: no cover - bench only
        return [f"  (could not identify: {e})"]
    probe_rs = find_probe_rs()
    if not probe_rs:
        return ["  (probe-rs not found; run: uv run tools/index/fetch_tools.py)"]

    records = []
    for attempt in range(attempts):
        records = chip_info.inventory(serial, probe_rs)
        if any(r["chip"] for r in records):
            break
        if attempt + 1 < attempts:
            time.sleep(gap)
    if not records:
        return ["  (the probe attached but probe-rs does not list it)"]
    return ["  " + line for line in chip_info.report(records)]


# --- reporting ----------------------------------------------------------------

def listing(probes: list, live: list) -> list:
    """One line per probe: name, serial, COM, bus id, and where it is."""
    names = nicknames()
    seen = {s.upper(): tty for s, tty in live}
    lines = [f"{'name':6} {'serial':14} {'COM':6} {'busid':7} state"]
    for probe in probes:
        tty = seen.get(probe["serial"])
        if tty:
            state = f"attached, {tty}"
        elif probe["attached"]:
            # Windows thinks it handed the device over and Linux has not bound
            # it. Usually means the attach is still settling; sometimes a wedged
            # probe, which detach + attach clears.
            state = "attached, not enumerated here yet"
        elif probe["busid"]:
            state = "plugged into Windows, not attached"
        else:
            state = "not plugged in"
        lines.append(f"{label(probe, names):6} {probe['serial']:14} "
                     f"{probe['com'] or '-':6} {probe['busid'] or '-':7} {state}")
    unknown = sorted(set(names.values()) - {p["serial"] for p in probes})
    for serial in unknown:
        lines.append(f"{[n for n, s in names.items() if s == serial][0]:6} "
                     f"{serial:14} {'-':6} {'-':7} named in .env, unknown to Windows")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(
        description="attach one WCH-Link to WSL over USB/IP, detaching the others")
    ap.add_argument("probe", nargs="?",
                    help="a CH32_PROBE_<NAME> nickname, a USB serial, or a prefix")
    ap.add_argument("--detach", action="store_true",
                    help="detach every WCH-Link and attach nothing")
    ap.add_argument("--no-identify", action="store_true",
                    help="skip the probe-rs question after switching")
    args = ap.parse_args()

    try:
        exe = usbipd()
        probes = windows_probes(exe)
        if not probes:
            raise Failure("Windows knows of no WCH-Link at all "
                          "(1a86:8010 RISC-V mode, 1a86:8012 ARM mode)")

        if args.detach:
            for probe in detach_all(exe, probes):
                print(f"detached {probe['serial']} ({probe['com'] or 'no COM'})")
            print("no WCH-Link attached")
            return 0

        if not args.probe:
            print("\n".join(listing(probes, here())))
            names = nicknames()
            if not names:
                print("\nNo names set. Put the serials in tests/.env as "
                      "CH32_PROBE_<NAME>=<serial> and run with "
                      "`uv run --env-file tests/.env`.")
            return 0

        wanted = resolve(args.probe, probes)
        # Checked before anything is detached. Asking for a probe that is not
        # plugged in is a typo away from asking for one that is, and the first
        # version of this script answered the typo by detaching the working
        # probe and then failing - leaving a bench with nothing on it.
        if not wanted["busid"]:
            raise Failure(f"{wanted['serial']} ({wanted['com'] or 'no COM'}) is "
                          f"known to Windows but not plugged in right now; "
                          f"nothing was detached")

        if wanted["attached"] and any(s.upper() == wanted["serial"] for s, _ in here()):
            print(f"{wanted['serial']} is already attached")
        else:
            gone = detach_all(exe, probes, keep=wanted["serial"])
            for probe in gone:
                print(f"detached {probe['serial']} ({probe['com'] or 'no COM'})")
            try:
                attach(exe, wanted)
            except Failure:
                # Put the bench back rather than leaving it empty because the
                # thing that was asked for could not be had.
                restore(exe, gone)
                raise
            tty = wait_for(wanted["serial"])
            if not tty:
                raise Failure(
                    f"{wanted['serial']} attached but did not enumerate in WSL "
                    f"within 15 s. A probe that stops answering is cleared by "
                    f"detaching and attaching it again, not by resetting it.")
            print(f"attached {wanted['serial']} "
                  f"({wanted['com'] or 'no COM'})  ->  {tty}")

        if not args.no_identify:
            print("\n".join(identify(wanted["serial"])))
        print(f"\nCH32_PROBE={wanted['serial']}   "
              f"(export it, or pass --probe {wanted['serial']})")
        return 0
    except Failure as e:
        print(e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
