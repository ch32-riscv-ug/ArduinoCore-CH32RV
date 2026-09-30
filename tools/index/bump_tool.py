#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Adopt a new version of a bundled tool: rebuild its tools_<name>.json from the release's assets.

    uv run tools/index/bump_tool.py ch32rv 0.12.3
    uv run tools/index/bump_tool.py ch32rv 0.12.3 --no-fetch

The fragment keeps its shape (hosts, comments, URL pattern); only the version, the tag and each
archive's checksum and size change, taken from the release's own <asset>.sha256 files and the
served Content-Length. Then the archive for this machine is fetched into <repo>/.tools through
tools/index/fetch_tools.py (checksum verified there) and `<tool> --version` is run, and a line is
added to CHANGELOG.md's Unreleased section for the editor to complete. Adopting a version is a
decision, so this is one command rather than a step in a release.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("tool", help="the fragment's name: tools_<tool>.json (e.g. ch32rv)")
    ap.add_argument("version", help="the new version (the release tag is v<version>)")
    ap.add_argument("--no-fetch", action="store_true", help="rewrite the fragment only")
    args = ap.parse_args()
    path = HERE / f"tools_{args.tool.replace('-', '_')}.json"
    if not path.exists():
        print(f"no fragment {path}", file=sys.stderr)
        return 2
    data = json.loads(path.read_text(encoding="utf-8"))
    old = data["version"]
    if old == args.version:
        print(f"{path.name} is already {old}")
    text = path.read_text(encoding="utf-8").replace(old, args.version)
    data = json.loads(text)
    seen: dict[str, tuple[str, str]] = {}
    for system in data["systems"]:
        url = system["url"]
        if url not in seen:
            sha = urllib.request.urlopen(url + ".sha256", timeout=60).read().decode().split()[0]
            head = urllib.request.Request(url, method="HEAD")
            size = urllib.request.urlopen(head, timeout=60).headers["Content-Length"]
            seen[url] = (sha, size)
            print(f"  {system['archiveFileName']}: {size} bytes, sha256 {sha[:12]}...")
        system["checksum"], system["size"] = "SHA-256:" + seen[url][0], seen[url][1]
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{path.name}: {old} -> {args.version}, {len(seen)} archives")
    if not args.no_fetch:
        subprocess.run([sys.executable, str(HERE / "fetch_tools.py"), "--tool", args.tool], check=True, cwd=REPO)
        exe = next((p for p in (REPO / ".tools" / args.tool / args.version).rglob(args.tool + "*") if p.is_file()
                    and p.suffix in ("", ".exe")), None)
        if exe:
            out = subprocess.run([str(exe), "--version"], capture_output=True, text=True).stdout.strip()
            print(f"  {exe.relative_to(REPO)} --version: {out}")
            if args.version not in out:
                print(f"  the binary does not say {args.version}", file=sys.stderr)
                return 1
    changelog = REPO / "CHANGELOG.md"
    s = changelog.read_text(encoding="utf-8")
    stub = (f"- (EN) The bundled {args.tool} is now {args.version}: <what changed for a user>.\n"
            f"- (JA) 同梱の {args.tool} を {args.version} にした: <利用者に見える変化>。\n")
    if f"{args.tool} is now {args.version}" not in s:
        s = s.replace("## Unreleased\n", "## Unreleased\n" + stub, 1)
        changelog.write_text(s, encoding="utf-8")
        print("CHANGELOG.md: a line to complete under Unreleased")
    return 0


if __name__ == "__main__":
    sys.exit(main())
