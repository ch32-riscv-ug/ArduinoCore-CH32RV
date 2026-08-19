#!/usr/bin/env python3
"""Fetch (and optionally extract) the xPack riscv-none-elf-gcc archive for this host.

Reads tools_xpack_gcc.json (single source of truth: URL, archiveFileName,
SHA-256, size), detects the current host, downloads into --dest if not already
present, verifies the checksum, and prints the resulting paths.

Used by CI and local scripts so the toolchain is cached by archive name and
never trusted without checksum verification.

Usage:
  fetch_xpack.py --dest <cache-dir> [--extract <dir>] [--print-bin]
"""
import argparse
import hashlib
import json
import pathlib
import platform
import sys
import tarfile
import urllib.request
import zipfile

HOST_KEYS = {
    ("Linux", "x86_64"): "x86_64-pc-linux-gnu",
    ("Linux", "aarch64"): "aarch64-linux-gnu",
    ("Darwin", "x86_64"): "x86_64-apple-darwin",
    ("Darwin", "arm64"): "arm64-apple-darwin",
    ("Windows", "AMD64"): "x86_64-mingw32",
}


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dest", required=True, type=pathlib.Path)
    ap.add_argument("--extract", type=pathlib.Path)
    ap.add_argument("--print-bin", action="store_true",
                    help="print the extracted bin directory path")
    args = ap.parse_args()

    frag = json.loads((pathlib.Path(__file__).parent / "tools_xpack_gcc.json")
                      .read_text(encoding="utf-8"))
    key = HOST_KEYS.get((platform.system(), platform.machine()))
    if key is None:
        print(f"unsupported host: {platform.system()}/{platform.machine()}",
              file=sys.stderr)
        return 1
    entry = next(s for s in frag["systems"] if s["host"] == key)
    want = entry["checksum"].split(":", 1)[1].lower()

    args.dest.mkdir(parents=True, exist_ok=True)
    archive = args.dest / entry["archiveFileName"]
    if not archive.exists():
        print(f"downloading {entry['url']}", file=sys.stderr)
        urllib.request.urlretrieve(entry["url"], archive)
    got = sha256(archive)
    if got != want:
        print(f"checksum mismatch for {archive}: got {got}, want {want}",
              file=sys.stderr)
        archive.unlink()
        return 1
    print(archive)

    if args.extract:
        root = args.extract / f"xpack-riscv-none-elf-gcc-{frag['version']}"
        if not root.exists():
            args.extract.mkdir(parents=True, exist_ok=True)
            if archive.suffix == ".zip":
                with zipfile.ZipFile(archive) as z:
                    z.extractall(args.extract)
            else:
                with tarfile.open(archive) as t:
                    t.extractall(args.extract)
        if args.print_bin:
            print(root / "bin")
    return 0


if __name__ == "__main__":
    sys.exit(main())
