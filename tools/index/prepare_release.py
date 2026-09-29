#!/usr/bin/env python3
"""Cut CHANGELOG.md's "## Unreleased" into "## X.Y.Z" (the release workflow's second step), and with
--body write that section alone to a file for the GitHub Release.

Usage: prepare_release.py --version X.Y.Z [--body release_body.md]
"""
from __future__ import annotations

import argparse
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHANGELOG = ROOT / "CHANGELOG.md"
NONE = ["- (EN) No user-facing changes recorded.", "- (JA) ユーザー向け変更の記録はありません。"]


def cut(version: str) -> None:
    lines = CHANGELOG.read_text(encoding="utf-8").splitlines()
    if f"## {version}" in lines:
        return                       # already cut (a re-run of the same release)
    try:
        top = lines.index("## Unreleased")
    except ValueError:
        raise SystemExit("CHANGELOG.md must contain '## Unreleased'")
    end = next((i for i in range(top + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    items = [line for line in lines[top + 1:end] if line.strip()]
    out = lines[:top + 1] + ["", f"## {version}"] + (items or NONE)
    if end < len(lines):
        out += [""] + lines[end:]
    CHANGELOG.write_text("\n".join(out).rstrip() + "\n", encoding="utf-8")


def body(version: str) -> str:
    lines = CHANGELOG.read_text(encoding="utf-8").splitlines()
    start = lines.index(f"## {version}") + 1
    end = next((i for i in range(start, len(lines)) if lines[i].startswith("## ")), len(lines))
    section = [line for line in lines[start:end]]
    while section and not section[0].strip():
        section.pop(0)
    while section and not section[-1].strip():
        section.pop()
    return "\n".join(section or NONE) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description="Prepare the CHANGELOG for a release.")
    ap.add_argument("--version", required=True)
    ap.add_argument("--body", type=pathlib.Path, help="write the version's section here")
    args = ap.parse_args()
    version = args.version.strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("version must be X.Y.Z")
    cut(version)
    if args.body:
        args.body.write_text(body(version), encoding="utf-8")
    print(f"CHANGELOG prepared for {version}")


if __name__ == "__main__":
    main()
