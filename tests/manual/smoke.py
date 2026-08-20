#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Compile, flash and read back the Milestone 1 acceptance sketch on one board.

It compiles and uploads exactly the way a user would - `arduino-cli upload
--programmer wch-link`, which drives probe-rs - so a pass means the shipping
path works, not just the code.

  uv run tests/manual/smoke.py --board CH32X035

The probe and its UART bridge are discovered by USB VID:PID, because boards get
swapped on this bench and /dev/ttyACM* is not stable. Pass --probe <serial> when
more than one probe is attached, or --port to override the discovery.

The board's Serial pins are printed before flashing: wire those two to the
probe's UART bridge (TX -> probe RX, RX -> probe TX) first.

Environment:
  CH32_GCC_BIN   riscv-none-elf-gcc bin directory (required)
  CH32_PROBE_RS  directory holding the probe-rs binary (default: the one the
                 Board Manager installed under ~/.arduino15)
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parents[2]
SKETCH = REPO / "tests" / "sketches" / "basic" / "serial_println" / "serial_println.ino"
EXPECT = ("hello from ch32", "int=42", "hex=BEEF")

# WCH-Link USB ids. RV mode is the one probe-rs drives; the same device also
# exposes a CDC UART bridge, so one cable carries both flashing and Serial.
WCH_LINK_VID = 0x1A86
WCH_LINK_PIDS = (0x8010, 0x8012)


def find_probes():
    """[(serial, tty)] for every attached WCH-Link, newest enumeration last."""
    from serial.tools import list_ports
    found = []
    for port in sorted(list_ports.comports(), key=lambda p: p.device):
        if port.vid == WCH_LINK_VID and port.pid in WCH_LINK_PIDS:
            found.append((port.serial_number or "", port.device))
    return found


def resolve_port(want_serial, want_port):
    """Pick the probe's UART bridge. Returns (port, serial) or exits."""
    if want_port and want_serial:
        return want_port, want_serial
    probes = find_probes()
    if want_serial:
        for serial_number, tty in probes:
            if serial_number == want_serial:
                return want_port or tty, serial_number
        raise SystemExit(f"no WCH-Link with serial {want_serial}; attached: "
                         f"{[s for s, _ in probes] or 'none'}")
    if want_port:
        return want_port, None
    if not probes:
        raise SystemExit("no WCH-Link found (looked for "
                         f"{WCH_LINK_VID:04x}:{'/'.join('%04x' % p for p in WCH_LINK_PIDS)})")
    if len(probes) > 1:
        raise SystemExit("several WCH-Links attached; pick one with --probe: "
                         + ", ".join(f"{s} ({t})" for s, t in probes))
    return probes[0][1], probes[0][0]


def serial_pins(board: str, override=None):
    """Read the chosen USART and its pins out of the generated variant."""
    header = (REPO / "variants" / board / "pins_arduino.h").read_text(encoding="utf-8")
    if override is not None:
        n = str(override)
        if f"#define CH32_SERIAL{n}_TX" not in header:
            raise SystemExit(f"{board}: the variant has no USART{n}")
    else:
        index = re.search(r"#define CH32_SERIAL_DEFAULT (\d+)", header)
        if not index:
            return None
        n = index.group(1)
    tx = re.search(rf"#define CH32_SERIAL{n}_TX (\w+)", header)
    rx = re.search(rf"#define CH32_SERIAL{n}_RX (\w+)", header)
    note = re.search(rf"/\* USART{n}: ([^*]+)\*/", header)
    return n, tx.group(1), rx.group(1), (note.group(1).strip() if note else "")


BENCH = REPO / "tests" / "hardware" / "bench.json"


def bench_serial(board: str):
    """The USART this bench has wired for a board, or None if unrecorded."""
    if not BENCH.exists():
        return None
    entry = json.loads(BENCH.read_text(encoding="utf-8"))["boards"].get(board)
    return entry.get("serial") if entry else None


def find_probe_rs():
    """The probe-rs the Board Manager installed, newest version last."""
    root = pathlib.Path.home() / ".arduino15" / "packages" / "ch32-riscv-ug" / "tools" / "probe-rs"
    found = sorted(d for d in root.glob("*") if (d / "probe-rs").exists()
                   or (d / "probe-rs.exe").exists())
    return str(found[-1]) if found else None


def detected_chip(probe_rs_dir, probe_serial):
    """What probe-rs thinks is attached, or None if it cannot tell."""
    cmd = [str(pathlib.Path(probe_rs_dir) / "probe-rs"), "info"]
    if probe_serial:
        cmd += ["--probe", f"1a86:8010:{probe_serial}"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("Detected chip:"):
            return line.split(":", 1)[1].strip()
    return None


def run(cmd, **kw):
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kw)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True, help="series board, e.g. CH32X035")
    ap.add_argument("--pnum", default="ANY")
    ap.add_argument("--port", help="probe UART bridge (default: discovered)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--probe", help="WCH-Link USB serial (default: the only one attached)")
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--force", action="store_true",
                    help="flash even if the attached chip is a different series")
    ap.add_argument("--serial", type=int,
                    help="override which USART Serial is (uart_scan.py finds it)")
    args = ap.parse_args()

    gcc = os.environ.get("CH32_GCC_BIN")
    if not gcc:
        print("set CH32_GCC_BIN", file=sys.stderr)
        return 2
    probe_rs_dir = os.environ.get("CH32_PROBE_RS") or find_probe_rs()
    if not probe_rs_dir:
        print("probe-rs not found; install the platform or set CH32_PROBE_RS",
              file=sys.stderr)
        return 2

    port, probe_serial = resolve_port(args.probe, args.port)
    print(f"== probe {probe_serial or '(unidentified)'} -> {port}")

    # Boards get swapped on this bench, so make a mismatch fail here rather
    # than as a mysterious silent target after flashing the wrong image.
    chip = detected_chip(probe_rs_dir, probe_serial)
    if chip:
        print(f"== target reports {chip}")
        if not chip.upper().startswith(args.board.upper()) and not args.force:
            print(f"FAIL: {chip} is attached but --board says {args.board}; "
                  f"pass --force to flash anyway")
            return 1
    else:
        print("== target chip not identified; flashing anyway")

    serial_index = args.serial if args.serial else bench_serial(args.board)
    if serial_index and not args.serial:
        print(f"== bench.json says {args.board} is wired to USART{serial_index}")
    pins = serial_pins(args.board, serial_index)
    if pins is None:
        print(f"{args.board}: the variant defines no default Serial port")
        return 1
    n, tx, rx, note = pins
    print(f"== {args.board}:{args.pnum}  Serial = USART{n}  TX={tx}  RX={rx}"
          f"{'  (' + note + ')' if note else ''}")
    print(f"   wire {tx} -> probe RX, {rx} -> probe TX, and a common ground")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        sketch_dir = tmp / SKETCH.stem
        sketch_dir.mkdir()
        shutil.copy(SKETCH, sketch_dir)
        build = tmp / "build"
        # Only the sketchbook is sandboxed: it is where the platform symlink
        # goes. The data directory is left alone so arduino-cli does not
        # re-download its builtin tools on every run.
        env = dict(os.environ, ARDUINO_DIRECTORIES_USER=str(tmp / "user"))
        (tmp / "user" / "hardware" / "ch32-riscv-ug").mkdir(parents=True)
        (tmp / "user" / "hardware" / "ch32-riscv-ug" / "ch32v").symlink_to(REPO)
        r = run(["arduino-cli", "compile",
                 "--fqbn", f"ch32-riscv-ug:ch32v:{args.board}:pnum={args.pnum}",
                 "--build-property", f"compiler.path={gcc}/",
                 *(["--build-property",
                    f"build.extra_flags=-DCH32_SERIAL_DEFAULT={serial_index}"]
                   if serial_index else []),
                 "--build-path", str(build), str(sketch_dir)], env=env)
        if r.returncode:
            print(r.stdout + r.stderr)
            return 1
        print("   " + r.stdout.strip().replace("\n", "\n   "))

        # The upload goes through the platform's own programmer entry, so this
        # exercises programmers.txt and the probe-rs recipe rather than a
        # side channel. --upload-property points the recipe at a probe-rs that
        # may not be Board-Manager-installed in a symlinked dev tree.
        upload = ["arduino-cli", "upload",
                  "--fqbn", f"ch32-riscv-ug:ch32v:{args.board}:pnum={args.pnum}",
                  "--programmer", "wch-link", "--input-dir", str(build),
                  "--upload-property", f"runtime.tools.probe-rs.path={probe_rs_dir}"]
        if probe_serial and args.probe:
            upload += ["--upload-property",
                       f"upload.probe_args=--probe 1a86:8010:{probe_serial}"]
        upload.append(str(sketch_dir))

        import serial
        with serial.Serial(port, args.baud, timeout=0.3) as uart:
            uart.reset_input_buffer()
            # The WCH-Link occasionally answers a flash session with
            # "bulk read timed out" and recovers on the next attempt. Retry
            # once, but say so: a bench that needs the retry every time is
            # broken, and hiding that would make the suite lie.
            r = run(upload, env=env)
            if r.returncode:
                print("   upload failed, retrying once:")
                print("   " + (r.stdout + r.stderr).strip().replace("\n", "\n   "))
                r = run(upload, env=env)
            if r.returncode:
                print(r.stdout + r.stderr)
                return 1
            print("   uploaded")
            deadline = time.time() + args.seconds
            got = b""
            while time.time() < deadline:
                got += uart.read(256)

    text = got.decode(errors="replace")
    print("--- output " + "-" * 48)
    print(text.strip() or "(nothing received)")
    print("-" * 59)
    missing = [w for w in EXPECT if w not in text]
    if missing:
        print(f"FAIL {args.board}: missing {missing}")
        return 1
    print(f"PASS {args.board}: Serial.println works")
    return 0


if __name__ == "__main__":
    sys.exit(main())
