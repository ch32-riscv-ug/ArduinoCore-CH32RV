#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""What is actually plugged into this bench right now?

Every other hardware test needs to know three things - which probe, which chip,
which serial port - and on this bench none of them are fixed: boards get
swapped, /dev/ttyACM* is assigned in enumeration order, and the probe's serial
number is the only stable handle. Guessing costs a wrong flash and a confusing
silent board, so this asks the hardware instead.

  cd tests && uv run pytest manual/chip_info/chip_info.py -v -s   # as a test
  uv run tests/manual/chip_info/chip_info.py               # as a report
  uv run tests/manual/chip_info/chip_info.py --probe FC92  # a prefix is enough

As a test it is the precondition for every other hardware run: a probe answers,
it identifies its chip, probe-rs knows that chip, boards.txt maps it, and the
generated variant gives it a Serial port. As a report it additionally prints the
exact commands that follow from the answer, which is what you want when setting
a board up by hand.

Reads only: it connects to the debug port and asks, it does not flash, reset or
run anything.

probe-rs comes from <repo>/.tools, which `uv run tools/index/fetch_tools.py`
fills. CH32_PROBE_RS overrides that, and CH32_PROBE picks one probe of several.
"""
import argparse
import csv
import os
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "manual" / "smoke"))

TARGETS_CSV = REPO / "tools" / "index" / "probe_rs_targets.csv"


class Failure(Exception):
    pass


def known_chips() -> set:
    """Every chip the generated probe-rs target table names."""
    if not TARGETS_CSV.exists():
        return set()
    with open(TARGETS_CSV, newline="", encoding="utf-8") as f:
        # The file opens with three comment lines. Without dropping them
        # DictReader takes the first as the header, every row["chip"] is None,
        # and the set comes back empty - which is how this check silently did
        # nothing until it became a test.
        rows = csv.DictReader(line for line in f if not line.startswith("#"))
        return {row["chip"].upper() for row in rows if row.get("chip")}


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


def inventory(probe=None, probe_rs_dir=None) -> list:
    """One record per attached WCH-Link, as data rather than as printed text.

    `chip` is None when the target did not answer, and `boards` is empty when
    nothing in boards.txt claims the chip - both are findings to report, not
    errors to raise, because a bench with two probes may have one of each.
    """
    from smoke import find_probes, find_probe_rs, detected_chip, boards_for

    probe_rs_dir = probe_rs_dir or find_probe_rs()
    if not probe_rs_dir:
        raise Failure("probe-rs not found; run: uv run tools/index/fetch_tools.py")

    probes = find_probes()
    if probe:
        probes = [p for p in probes if p[0].startswith(probe)]
    known = known_chips()

    records = []
    for serial_number, tty in probes:
        # One probe attached means probe-rs needs no selector, and asking
        # without one also works when the probe reports no serial number.
        chip = detected_chip(probe_rs_dir, serial_number if len(probes) > 1 else None)
        boards = {}
        for board, pnums in sorted(boards_for(chip).items() if chip else []):
            pnum = chip if chip in pnums else "ANY"
            boards[board] = {
                "pnum": pnum,
                "fqbn": f"ch32-riscv-ug:ch32v:{board}:pnum={pnum}",
                "serial": serial_pins_of(board),
            }
        records.append({
            "probe": serial_number,
            "tty": tty,
            "chip": chip,
            # None rather than False when the table is missing entirely: "not
            # listed" and "there is no list" are different problems.
            "in_targets_csv": (chip.upper() in known) if chip and known else None,
            "boards": boards,
        })
    return records


def report(records: list) -> list:
    """The human-facing lines, including what to run next."""
    lines = []
    several = len(records) > 1
    for r in records:
        serial_number, chip = r["probe"], r["chip"]
        lines.append(f"probe {serial_number or '(no serial number)'}")
        lines.append(f"  UART bridge     {r['tty']}")
        if not chip:
            lines.append("  chip            not detected (target unpowered, SWD "
                         "not wired, or the part is unknown to this probe-rs)")
            continue
        lines.append(f"  chip            {chip}")
        if r["in_targets_csv"] is False:
            lines.append("                  (not in tools/index/probe_rs_targets.csv"
                         " - regenerate it against this probe-rs version)")
        if not r["boards"]:
            lines.append("  board           no boards.txt entry maps to this chip "
                         "[compile only]")
            continue
        for board, hit in r["boards"].items():
            pins = hit["serial"]
            lines.append(f"  board           {board}")
            if pins:
                lines.append(f"  Serial          USART{pins[0]}  TX={pins[1]}  "
                             f"RX={pins[2]}   (TX -> probe RX, RX -> probe TX, "
                             f"common ground)")
            lines.append(f"  FQBN            {hit['fqbn']}")
            # Several probes attached means every command needs the selector,
            # so print it assembled rather than making the reader do it.
            cmd = "uv run tests/manual/smoke/smoke.py"
            if several:
                cmd += f" --probe {serial_number}"
            lines.append("  next            " + cmd)
            if several and serial_number:
                lines.append(f'                  upload needs: --upload-property '
                             f'upload.probe_args="--probe 1a86:8010:{serial_number}"')
    return lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", default=os.environ.get("CH32_PROBE"),
                    help="probe serial number, or any prefix of it")
    ap.add_argument("--probe-rs", help="directory holding the probe-rs binary")
    args = ap.parse_args()

    try:
        records = inventory(args.probe, args.probe_rs)
    except Failure as e:
        print(e, file=sys.stderr)
        return 2
    if not records:
        print("no WCH-Link attached"
              + (f" whose serial starts with {args.probe}" if args.probe else ""))
        return 1
    print("\n".join(report(records)))
    return 0 if all(r["boards"] for r in records) else 1


# --- as a test ---------------------------------------------------------------
# The bench preconditions, one assertion each, so a broken bench says which part
# is broken. `attached` lives in manual/conftest.py and skips when nothing is
# plugged in.

def test_report_is_printed(attached):
    """Not an assertion - it puts the bench in the log of every run."""
    print("\n" + "\n".join(report(attached)))


def test_every_probe_identifies_its_chip(attached):
    """A probe that answers but names no chip means the target is not reachable."""
    silent = [r["probe"] or r["tty"] for r in attached if not r["chip"]]
    assert not silent, (f"{silent} did not identify a chip: target unpowered, "
                        f"SWD not wired, or the part is unknown to this probe-rs")


def test_every_chip_is_in_the_generated_targets_table(attached):
    """tools/index/probe_rs_targets.csv has to cover what the bench can see."""
    assert known_chips(), f"{TARGETS_CSV} is missing or empty"
    missing = [r["chip"] for r in attached if r["in_targets_csv"] is False]
    assert not missing, (f"{missing} not in {TARGETS_CSV.name}; regenerate it "
                         f"against this probe-rs version")


def test_every_chip_maps_to_a_board(attached):
    """boards.txt claims the silicon, via {build.probe_rs_chip} rather than by name."""
    unmapped = [r["chip"] for r in attached if r["chip"] and not r["boards"]]
    assert not unmapped, (f"no boards.txt entry maps to {unmapped} - those parts "
                          f"are compile-only until one does")


def test_every_board_has_a_default_serial_port(attached):
    """The variant generator picked a Serial route, so smoke.py has one to use."""
    without = [board for r in attached for board, hit in r["boards"].items()
               if hit["serial"] is None]
    assert not without, f"the generated variant for {without} defines no Serial"


if __name__ == "__main__":
    sys.exit(main())
