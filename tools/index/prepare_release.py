#!/usr/bin/env python3
"""Move CHANGELOG.md's `## Unreleased` items under `## <version>` and write that section as the GitHub Release's body.

The same shape as host-arduino-core's scripts/prepare_release.py. Run again for a version already in CHANGELOG.md (a
re-run of the release workflow), it leaves the file as it is and only writes the body.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHANGELOG = ROOT / "CHANGELOG.md"
UNRELEASED = "## Unreleased"


def _section_end(lines: list[str], start: int) -> int:
    for i in range(start, len(lines)):
        if lines[i].startswith("## "):
            return i
    return len(lines)


def update_changelog(lines: list[str], version: str) -> list[str]:
    if f"## {version}" in lines:
        return lines
    try:
        head = lines.index(UNRELEASED)
    except ValueError as exc:
        raise SystemExit(f"CHANGELOG.md must contain '{UNRELEASED}'") from exc
    tail = _section_end(lines, head + 1)
    items = [line for line in lines[head + 1:tail] if line.strip()]
    if not items:
        items = ["- (EN) No user-facing changes recorded.", "- (JA) ユーザー向け変更の記録はありません。"]
    out = lines[:head + 1] + ["", f"## {version}"] + items
    if tail < len(lines):
        out += [""] + lines[tail:]
    return out


def release_body(lines: list[str], version: str) -> str:
    start = lines.index(f"## {version}") + 1
    body = lines[start:_section_end(lines, start)]
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    return "\n".join(body) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="the version to release, X.Y.Z")
    parser.add_argument("--body", type=Path, help="write the version's section here (the Release's body)")
    args = parser.parse_args()
    version = args.version.strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("version must be X.Y.Z")

    lines = update_changelog(CHANGELOG.read_text(encoding="utf-8").splitlines(), version)
    CHANGELOG.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    if args.body:
        args.body.write_text(release_body(lines, version), encoding="utf-8")
    print(f"CHANGELOG.md: {version}")


if __name__ == "__main__":
    main()
