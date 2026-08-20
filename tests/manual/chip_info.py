#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""What is actually plugged into this bench right now?

Every other hardware script needs to know three things - which probe, which
chip, which serial port - and on this bench none of them are fixed: boards get
swapped, /dev/ttyACM* is assigned in enumeration order, and the probe's serial
number is the only stable handle. Guessing costs a wrong flash and a confusing
silent board, so this asks the hardware instead and prints the exact commands
that follow from the answer.

  uv run tests/manual/chip_info.py               # everything attached
  uv run tests/manual/chip_info.py --probe FC92  # one probe, prefix is enough

Reads only: it connects to the debug port and asks, it does not flash, reset or
run anything.

Environment:
  CH32_PROBE_RS  directory holding the probe-rs binary (default: the one the
                 Board Manager installed under ~/.arduino15)
"""
import argparse
import csv
import os
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual"))

TARGETS_CSV = REPO / "tools" / "index" / "probe_rs_targets.csv"


def serial_pins_of(board: str):
    """(usart, tx pad, rx pad) the generated variant picked for Serial."""
    header = REPO / "variants" / board / "pins_arduino.h"
    if not header.exists():
        return None
    text = header.read_text(encoding="utf-8")
    n = re.search(r"^#define\s+CH32_SERIAL_DEFAULT\s+(\d+)", text, re.M)
    if not n:
        return None
    index = n.group(1)
    pads = []
    for direction in ("TX", "RX"):
        m = re.search(rf"^#define\s+CH32_SERIAL{index}_{direction}\s+(\w+)", text, re.M)
        pads.append(m.group(1) if m else "?")
    return index, pads[0], pads[1]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", help="probe serial number, or any prefix of it")
    ap.add_argument("--probe-rs", help="directory holding the probe-rs binary")
    args = ap.parse_args()

    from smoke import find_probes, find_probe_rs, detected_chip, boards_for

    probe_rs_dir = args.probe_rs or os.environ.get("CH32_PROBE_RS") or find_probe_rs()
    if not probe_rs_dir:
        print("probe-rs not found; install the platform or set CH32_PROBE_RS",
              file=sys.stderr)
        return 2

    probes = find_probes()
    if args.probe:
        probes = [p for p in probes if p[0].startswith(args.probe)]
    if not probes:
        print("no WCH-Link attached"
              + (f" whose serial starts with {args.probe}" if args.probe else ""))
        return 1

    known = set()
    if TARGETS_CSV.exists():
        with open(TARGETS_CSV, newline="", encoding="utf-8") as f:
            known = {row["chip"].upper() for row in csv.DictReader(f) if row.get("chip")}

    failures = 0
    for serial_number, tty in probes:
        print(f"probe {serial_number or '(no serial number)'}")
        print(f"  UART bridge     {tty}")
        # Several probes attached means every command needs --probe, so print
        # the identifier in the form probe-rs wants rather than making the
        # reader assemble it.
        selector = f"1a86:8010:{serial_number}" if serial_number else None
        chip = detected_chip(probe_rs_dir, serial_number if len(probes) > 1 else None)
        if not chip:
            print("  chip            not detected (target unpowered, SWD not "
                  "wired, or the part is unknown to this probe-rs)")
            failures += 1
            continue
        print(f"  chip            {chip}")
        if known and chip.upper() not in known:
            print("                  (not in tools/index/probe_rs_targets.csv - "
                  "regenerate it against this probe-rs version)")

        hits = boards_for(chip)
        if not hits:
            print("  board           no boards.txt entry maps to this chip "
                  "[compile only]")
            failures += 1
            continue
        for board, pnums in sorted(hits.items()):
            pnum = chip if chip in pnums else "ANY"
            pins = serial_pins_of(board)
            print(f"  board           {board}")
            if pins:
                print(f"  Serial          USART{pins[0]}  TX={pins[1]}  RX={pins[2]}"
                      f"   (TX -> probe RX, RX -> probe TX, common ground)")
            print(f"  FQBN            ch32-riscv-ug:ch32v:{board}:pnum={pnum}")
            cmd = "    uv run tests/manual/smoke.py"
            if len(probes) > 1:
                cmd += f" --probe {serial_number}"
            print("  next            " + cmd.strip())
            if selector and len(probes) > 1:
                print(f"                  upload needs: --upload-property "
                      f'upload.probe_args="--probe {selector}"')
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
