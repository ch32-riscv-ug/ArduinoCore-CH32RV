#!/usr/bin/env python3
"""Compile, flash and read back the Milestone 1 acceptance sketch on one board.

This is the bring-up path for a board the core has never run on. It does not go
through `arduino-cli upload`, because the platform has no programmer definition
yet (Q-040/Q-044); it calls minichlink directly. Once the programmer lands, the
same check runs from pytest through the sketch profiles instead.

  tests/hardware/smoke.py --board CH32X035 --port /dev/ttyACM4

The board's Serial pins are printed before flashing: wire those two to the
probe's UART bridge (TX -> probe RX, RX -> probe TX) first.

Environment:
  CH32_GCC_BIN    riscv-none-elf-gcc bin directory (required)
  CH32_MINICHLINK path to minichlink (required)
"""
import argparse
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


def serial_pins(board: str):
    """Read the chosen USART and its pins out of the generated variant."""
    header = (REPO / "variants" / board / "pins_arduino.h").read_text(encoding="utf-8")
    index = re.search(r"#define CH32_SERIAL_DEFAULT (\d+)", header)
    if not index:
        return None
    n = index.group(1)
    tx = re.search(rf"#define CH32_SERIAL{n}_TX (\w+)", header)
    rx = re.search(rf"#define CH32_SERIAL{n}_RX (\w+)", header)
    note = re.search(rf"/\* USART{n}: ([^*]+)\*/", header)
    return n, tx.group(1), rx.group(1), (note.group(1).strip() if note else "")


def run(cmd, **kw):
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kw)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True, help="series board, e.g. CH32X035")
    ap.add_argument("--pnum", default="ANY")
    ap.add_argument("--port", default="/dev/ttyACM4", help="probe UART bridge")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--probe", help="WCH-Link USB serial; needs a minichlink with -l")
    ap.add_argument("--seconds", type=float, default=4.0)
    args = ap.parse_args()

    gcc = os.environ.get("CH32_GCC_BIN")
    minichlink = os.environ.get("CH32_MINICHLINK")
    if not gcc or not minichlink:
        print("set CH32_GCC_BIN and CH32_MINICHLINK", file=sys.stderr)
        return 2

    pins = serial_pins(args.board)
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
                 "--build-path", str(build), str(sketch_dir)], env=env)
        if r.returncode:
            print(r.stdout + r.stderr)
            return 1
        print("   " + r.stdout.strip().replace("\n", "\n   "))

        binary = build / f"{SKETCH.stem}.ino.bin"
        select = ["-l", args.probe] if args.probe else []
        r = run([minichlink, *select, "-w", str(binary), "flash", "-a"])
        if "Image written" not in (r.stdout + r.stderr):
            print("flash failed:\n" + r.stdout + r.stderr)
            return 1
        print("   flashed")

        try:
            import serial
        except ImportError:
            print("pyserial is not installed; skipping the read-back")
            return 1
        with serial.Serial(args.port, args.baud, timeout=0.3) as port:
            port.reset_input_buffer()
            run([minichlink, *select, "-b"])          # release from halt
            deadline = time.time() + args.seconds
            got = b""
            while time.time() < deadline:
                got += port.read(256)

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
