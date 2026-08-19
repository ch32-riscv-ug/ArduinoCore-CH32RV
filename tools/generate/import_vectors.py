#!/usr/bin/env python3
"""Rebuild tools/generate/interrupts/interrupts.csv from the EVT mirrors.

This is a maintenance tool, not part of the build. The committed CSV is the
input to generate.py; EVT is only read here and by the startup equivalence
harness (tests/startup/), which re-verifies every PR that the generated vector
tables still match what the silicon documentation describes.

Usage:
  import_vectors.py --mirrors <dir containing CH32* EVT mirrors> [--check]
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import re
import sys

# variant tag -> EVT startup .S, relative to the mirror root.
# Keep in sync with the CONFIG table in tests/startup/run_check.sh.
SOURCES = {
    "v003":     "CH32V003/EVT/EXAM/SRC/Startup/startup_ch32v00x.S",
    "v00x":     "CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S",
    "v20x_d6":  "CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D6.S",
    "v20x_d8":  "CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D8.S",
    "v20x_d8w": "CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D8W.S",
    "v205":     "CH32V205/EVT/EXAM/SRC/Startup/startup_ch32v205.S",
    "m030":     "CH32M030/EVT/EXAM/SRC/Startup/startup_ch32m030.S",
    "v307_d8":  "CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8.S",
    "v307_d8c": "CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8C.S",
    "v4x7":     "CH32V407/EVT/EXAM/SRC/Startup/startup_ch32v4x7.S",
    "x035":     "CH32X035/EVT/EXAM/SRC/Startup/startup_ch32x035.S",
    "x3x5":     "CH32X315/EVT/EXAM/SRC/Startup/startup_ch32x3x5.S",
    "l103":     "CH32L103/EVT/EXAM/SRC/Startup/startup_ch32l103.S",
}

OUT = pathlib.Path(__file__).parent / "interrupts" / "interrupts.csv"


def parse_startup(path: pathlib.Path) -> list[str | None]:
    """Return the vector entries after the reset slot; None means reserved."""
    entries: list[str] = []
    in_table = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        s = line.strip()
        if not in_table:
            if s.startswith(".option") and "norvc" in s:
                in_table = True
            continue
        if s.startswith(".option") and "rvc" in s and "norvc" not in s:
            break
        m = re.match(r"\.word\s+([A-Za-z0-9_]+)", s)
        if m:
            entries.append(m.group(1))
            continue
        m = re.match(r"j\s+([A-Za-z0-9_]+)", s)
        if m:
            entries.append("@J@" + m.group(1))
    # Slot 0 is the reset vector; crt0_ch32.S emits it.
    if entries and (entries[0] in ("_start", "0") or entries[0].startswith("@J@")):
        entries = entries[1:]
    out: list[str | None] = []
    for e in entries:
        if e.startswith("@J@"):
            raise SystemExit(f"{path}: j-form vector entry '{e}' is not supported")
        out.append(None if e == "0" else e)
    return out


def build(mirrors: pathlib.Path) -> list[tuple[str, int, str]]:
    rows: list[tuple[str, int, str]] = []
    for variant, rel in SOURCES.items():
        src = mirrors / rel
        if not src.is_file():
            raise SystemExit(f"missing EVT startup: {src}")
        for slot, handler in enumerate(parse_startup(src), start=1):
            rows.append((variant, slot, handler or ""))
    return rows


def write(rows: list[tuple[str, int, str]]) -> str:
    lines = [
        "# DO NOT EDIT BY HAND - rebuild with tools/generate/import_vectors.py",
        "# Interrupt vector map per startup variant. Slot 0 (reset) is emitted by",
        "# crt0_ch32.S, so slots start at 1. An empty handler means a reserved slot.",
        "# Verified against the EVT startup sources every PR by tests/startup/.",
        "variant,slot,handler",
    ]
    lines += [f"{v},{s},{h}" for v, s, h in rows]
    return "\n".join(lines) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mirrors", required=True, type=pathlib.Path)
    ap.add_argument("--check", action="store_true",
                    help="fail if the committed CSV differs instead of rewriting it")
    args = ap.parse_args()

    text = write(build(args.mirrors))
    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print(f"DRIFT: {OUT}", file=sys.stderr)
            raise SystemExit(1)
        print(f"ok:    {OUT}")
        return
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    n = len(text.splitlines()) - 5
    print(f"wrote: {OUT} ({n} rows, {len(SOURCES)} variants)")


if __name__ == "__main__":
    main()
