#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Put every tool the tests need inside the project, at a predictable path.

The tests need a RISC-V toolchain, probe-rs and the ch32-device-data tables.
Before this they came from environment variables pointing at wherever the
author happened to have unpacked them, which is not something another machine
can reproduce - and on Windows the paths differ enough that "wherever" is a
guess. So they go under <repo>/.tools instead: same layout on every host,
gitignored, and nothing outside the project is touched.

  uv run tools/index/fetch_tools.py            # everything, into <repo>/.tools
  uv run tools/index/fetch_tools.py --tool probe-rs
  uv run tools/index/fetch_tools.py --print-paths

Versions are not written here. They come from the tool definition fragments in
this directory (tools_*.json), which are the same files the published package
index is built from - so the tests run against exactly the versions a user
would install, and there is no second list to keep in step. Every download is
checked against the SHA-256 recorded there before it is unpacked.

Layout, mirroring how arduino-cli lays tools out so the two are interchangeable:

    .tools/<tool name>/<version>/...        the archive's contents, root folder
                                            flattened away as arduino-cli does
    .tools/ch32-device-data/tables/         the device tables, at their locked commit
    .tools/cache/                           downloaded archives, kept for re-runs

Nothing here is required at runtime by the platform itself; this is only for
running the tests and the generator.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_ROOT = REPO / ".tools"

HOST_KEYS = {
    ("Linux", "x86_64"): "x86_64-pc-linux-gnu",
    ("Linux", "aarch64"): "aarch64-linux-gnu",
    ("Darwin", "x86_64"): "x86_64-apple-darwin",
    ("Darwin", "arm64"): "arm64-apple-darwin",
    ("Windows", "AMD64"): "x86_64-mingw32",
    ("Windows", "x86"): "i686-mingw32",
}

DEVICE_DATA = "ch32-device-data"
DEVICE_DATA_URL = f"https://github.com/ch32-riscv-ug/{DEVICE_DATA}"


def host_key() -> str:
    key = HOST_KEYS.get((platform.system(), platform.machine()))
    if key is None:
        raise SystemExit(f"unsupported host: {platform.system()}/"
                         f"{platform.machine()}")
    return key


def fragments() -> dict:
    """{tool name: fragment} for every tools_*.json beside this script."""
    out = {}
    for path in sorted(HERE.glob("tools_*.json")):
        frag = json.loads(path.read_text(encoding="utf-8"))
        out[frag["name"]] = frag
    return out


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(entry: dict, cache: pathlib.Path) -> pathlib.Path:
    """Fetch into the cache and verify. A bad archive is deleted, not kept."""
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / entry["archiveFileName"]
    want = entry["checksum"].split(":", 1)[1].lower()
    if archive.exists() and sha256(archive) == want:
        return archive
    print(f"  downloading {entry['url']}", file=sys.stderr)
    tmp = archive.with_suffix(archive.suffix + ".part")
    urllib.request.urlretrieve(entry["url"], tmp)      # noqa: S310
    got = sha256(tmp)
    if got != want:
        tmp.unlink()
        raise SystemExit(f"checksum mismatch for {entry['archiveFileName']}: "
                         f"got {got}, want {want}")
    tmp.replace(archive)
    return archive


def unpack(archive: pathlib.Path, dest: pathlib.Path) -> None:
    """Extract, flattening a single root folder if the archive has one.

    arduino-cli does the same, so a tool unpacked here sits at the same depth
    as the same tool installed by Board Manager. That is what lets the scripts
    accept either without special cases.
    """
    # Unpack beside the destination, then move: a half-extracted tool must
    # never be left looking complete. Same filesystem, so the move is atomic.
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest.parent) as tmp:
        tmp = pathlib.Path(tmp)
        if archive.name.endswith(".zip"):
            with zipfile.ZipFile(archive) as z:
                z.extractall(tmp)
        else:
            with tarfile.open(archive) as t:
                # "tar" keeps the permission bits a toolchain needs (the
                # stricter "data" filter clamps them) while still refusing
                # absolute paths and traversal. The archive is already
                # checksum-verified at this point.
                try:
                    t.extractall(tmp, filter="tar")
                except TypeError:       # filter= predates neither 3.10.12 nor 3.11.4
                    t.extractall(tmp)
        entries = list(tmp.iterdir())
        root = entries[0] if len(entries) == 1 and entries[0].is_dir() else tmp
        if dest.exists():
            shutil.rmtree(dest)
        shutil.move(str(root), str(dest))


def locked_commit() -> str | None:
    """The ch32-device-data commit the generated files were produced from.

    boards.txt records it, so the tables can be checked out at exactly the
    revision this working tree was generated against rather than at whatever
    main happens to be.
    """
    header = (REPO / "boards.txt").read_text(encoding="utf-8")[:2000]
    m = re.search(r"ch32-device-data tables @ git ([0-9a-f]{40})", header)
    return m.group(1) if m else None


def fetch_device_data(root: pathlib.Path) -> pathlib.Path:
    dest = root / DEVICE_DATA
    commit = locked_commit()
    if not dest.exists():
        print(f"  cloning {DEVICE_DATA_URL}", file=sys.stderr)
        subprocess.run(["git", "clone", "--quiet", DEVICE_DATA_URL, str(dest)],
                       check=True)
    if commit:
        have = subprocess.run(["git", "-C", str(dest), "rev-parse", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        if have != commit:
            # Fetch first: a shallow or stale clone may not have it yet.
            subprocess.run(["git", "-C", str(dest), "fetch", "--quiet", "origin"],
                           check=False)
            subprocess.run(["git", "-C", str(dest), "checkout", "--quiet", commit],
                           check=True)
            print(f"  checked out locked commit {commit[:12]}", file=sys.stderr)
    else:
        print("  boards.txt records no locked commit; leaving the clone as is",
              file=sys.stderr)
    return dest / "tables"


def tool_dir(root: pathlib.Path, name: str, version: str) -> pathlib.Path:
    return root / name / version


def paths(root: pathlib.Path) -> dict:
    """Where each thing ends up. Used by --print-paths and by the scripts."""
    frags = fragments()
    out = {}
    for name, frag in frags.items():
        out[name] = tool_dir(root, name, frag["version"])
    out[DEVICE_DATA] = root / DEVICE_DATA / "tables"
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=pathlib.Path, default=DEFAULT_ROOT,
                    help=f"where to put everything (default: {DEFAULT_ROOT})")
    ap.add_argument("--tool", action="append", default=None,
                    help="only this tool; repeatable. Default: all of them, "
                         "plus the device-data tables")
    ap.add_argument("--print-paths", action="store_true",
                    help="print name=path for each tool and exit without fetching")
    ap.add_argument("--print-env", action="store_true",
                    help="print the export lines for the override variables")
    args = ap.parse_args()

    frags = fragments()
    known = sorted(frags) + [DEVICE_DATA]
    wanted = args.tool or known
    for name in wanted:
        if name not in known:
            raise SystemExit(f"unknown tool {name!r}; known: {', '.join(known)}")

    where = paths(args.root)
    if args.print_paths:
        for name in wanted:
            print(f"{name}={where[name]}")
        return 0
    if args.print_env:
        gcc = where.get("xpack-riscv-none-elf-gcc")
        print(f"export CH32_GCC_BIN={gcc / 'bin'}")
        print(f"export CH32_PROBE_RS={where['probe-rs']}")
        print(f"export CH32_TABLES={where[DEVICE_DATA]}")
        return 0

    key = host_key()
    cache = args.root / "cache"
    for name in wanted:
        if name == DEVICE_DATA:
            print(f"{DEVICE_DATA}:", file=sys.stderr)
            print(fetch_device_data(args.root))
            continue
        frag = frags[name]
        dest = tool_dir(args.root, name, frag["version"])
        print(f"{name} {frag['version']}:", file=sys.stderr)
        if dest.exists():
            print(f"  already at {dest}", file=sys.stderr)
            print(dest)
            continue
        entry = next((s for s in frag["systems"] if s["host"] == key), None)
        if entry is None:
            raise SystemExit(f"{name} has no entry for this host ({key})")
        unpack(download(entry, cache), dest)
        print(dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
