#!/usr/bin/env python3
"""Set the platform version everywhere it is written down (the release workflow's first step).

- platform.txt `version=` - the one source: gen_index.py and tests/sketches/sync_profiles.py read it.
- every sketch.yaml's `- platform: ch32-riscv-ug:ch32v (X.Y.Z)` pin: the generated ones (tests/sketches,
  libraries/*/examples, re-checked by sync_profiles.py --check) and the hand-written ones under tests/manual.

Usage: bump_version.py X.Y.Z
"""
from __future__ import annotations

import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
PLATFORM_TXT = ROOT / "platform.txt"
PIN_RE = re.compile(r"^(\s*-\s*platform:\s*ch32-riscv-ug:ch32v\s*)\([^)]*\)(\s*)$")
SKIP = {".git", ".tools", ".venv", "build"}


def update_platform_version(version: str) -> None:
    lines = PLATFORM_TXT.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        if line.startswith("version="):
            lines[i] = f"version={version}"
            PLATFORM_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return
    raise SystemExit("platform.txt must contain a 'version=' entry")


def update_sketch_pins(version: str) -> tuple[int, int]:
    matched = changed = 0
    for path in sorted(ROOT.rglob("sketch.yaml")):
        if SKIP & set(path.relative_to(ROOT).parts):
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        dirty = False
        for i, line in enumerate(lines):
            if PIN_RE.match(line):
                matched += 1
                new = PIN_RE.sub(rf"\g<1>({version})\g<2>", line)
                if new != line:
                    lines[i] = new
                    dirty = True
                    changed += 1
        if dirty:
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return matched, changed


def main() -> None:
    ap = argparse.ArgumentParser(description="Set the ArduinoCore-CH32 platform version.")
    ap.add_argument("version", help="release version, X.Y.Z")
    version = ap.parse_args().version.strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("version must be X.Y.Z")
    update_platform_version(version)
    matched, changed = update_sketch_pins(version)
    print(f"platform.txt version={version}; sketch.yaml pins: {matched} checked, {changed} updated")


if __name__ == "__main__":
    main()
