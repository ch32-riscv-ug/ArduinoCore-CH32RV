#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""libraries/FS/src/fatfs holds FatFs R0.16 with its author's patches.

docs/filesystem.ja.md adopts FatFs as the file system of the FS library. As
with any vendored source (docs/vendor-policy.ja.md), every file is hashed so
a local edit cannot hide among upstream code.

  uv run tools/vendor/vendor_fatfs.py --update      # fetch and re-vendor
  uv run tools/vendor/vendor_fatfs.py --check       # verify against the lock

--check is offline. --update needs the network and the `patch` command.

What comes in: ff.c, ff.h, diskio.h and ffunicode.c from the release, and its
LICENSE.txt. ffconf.h (the configuration) and the disk glue are ours, in
src/ next to fatfs/; diskio.c (a template) and ffsystem.c (the allocator and
mutex hooks of configurations we do not use) stay out.

The author publishes fixes as diffs against the release rather than as new
releases (https://elm-chan.org/fsw/ff/patches.html). Both of R0.16's are
applied, in order: patch 1 (64-sector minimum volume) and patch 2 (on top of
1; CVE-2026-6682, -6683 and -6687 - a broken BPB or exFAT label from a card
could crash or overflow). The release is CRLF and the diffs LF, so the diffs
are applied with CRLF line ends; the files keep upstream's CRLF.
"""
import argparse
import hashlib
import io
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = REPO / "vendor" / "fatfs.lock.toml"
DEST = REPO / "libraries" / "FS" / "src"
VENDORED_DIR = "fatfs"

VERSION = "R0.16"
URL = "https://elm-chan.org/fsw/ff/arc/ff16.zip"
ARCHIVE_SHA256 = "99f7dc1f7e095356e4a9e3dbe29959090d8b948afe2bbc5441e52fdf4b85449e"
PATCHES = (
    {"url": "https://elm-chan.org/fsw/ff/patch/ff16p1.diff",
     "sha256": "7996ddc3135f3d534a8153f7d75d587030e5c8551b003c3b7ee823ffb7fc64bf",
     "why": "upstream patch 1: minimum volume 64 sectors"},
    {"url": "https://elm-chan.org/fsw/ff/patch/ff16p2.diff",
     "sha256": "5cd39f1fc299f0f1dec9fa1ce544dc0bc048b2f6eb3b8161476ed20f9cf1e290",
     "why": "upstream patch 2: CVE-2026-6682, -6683, -6687"},
)
TAKEN = {"source/ff.c": "ff.c", "source/ff.h": "ff.h", "source/diskio.h": "diskio.h",
         "source/ffunicode.c": "ffunicode.c", "LICENSE.txt": "LICENSE.txt"}


class Failure(Exception):
    pass


def download(url: str, sha256: str) -> bytes:
    print(f"fetching {url}", flush=True)
    with urllib.request.urlopen(url, timeout=120) as response:
        blob = response.read()
    digest = hashlib.sha256(blob).hexdigest()
    if digest != sha256:
        raise Failure(f"{url}: sha256 {digest} is not the pinned {sha256}")
    return blob


def fetch() -> dict:
    """{fatfs-relative path: bytes}: the release's files with the patches applied."""
    blob = download(URL, ARCHIVE_SHA256)
    out = {}
    with zipfile.ZipFile(io.BytesIO(blob)) as z:
        for name, target in TAKEN.items():
            out[target] = z.read(name)
    if not shutil.which("patch"):
        raise Failure("the `patch` command is needed to apply the author's diffs")
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        (work / "ff.c").write_bytes(out["ff.c"])
        for p in PATCHES:
            diff = download(p["url"], p["sha256"]).replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            subprocess.run(["patch", "--binary", "--silent", "--forward", "ff.c"], input=diff, cwd=work, check=True)
        out["ff.c"] = (work / "ff.c").read_bytes()
    if b"w/patch 2" not in out["ff.c"][:200]:
        raise Failure("ff.c does not say it carries patch 2")
    return out


def write_lock(files: dict) -> None:
    lines = [
        "# FatFs vendored snapshot lock",
        "# Regenerate/verify: tools/vendor/vendor_fatfs.py",
        "# See docs/filesystem.ja.md",
        "",
        "[[source]]",
        'id = "fatfs"',
        'url = "https://elm-chan.org/fsw/ff/"',
        f'tag = "{VERSION}"',
        f'archive = "{URL}"',
        f'archive_sha256 = "{ARCHIVE_SHA256}"',
        'license = "FatFs (BSD-style, 1 clause)"',
        f'license_file = "{(DEST / VENDORED_DIR / "LICENSE.txt").relative_to(REPO).as_posix()}"',
        f'dest = "{DEST.relative_to(REPO).as_posix()}"',
        "# Only this directory under dest is upstream; the rest of dest is the FS library.",
        f'dirs = ["{VENDORED_DIR}"]',
        "# The author's own fixes to the release, applied in order to ff.c (see the tool's docstring).",
        "patches = [",
    ]
    for p in PATCHES:
        lines.append(f'  {{ url = "{p["url"]}", sha256 = "{p["sha256"]}", why = "{p["why"]}" }},')
    lines += ["]", "", "# SHA-256 of every vendored file (after the patches), relative to dest.", "files = ["]
    for name in sorted(files):
        lines.append(f'  {{ path = "{VENDORED_DIR}/{name}", sha256 = "{files[name]}" }},')
    lines += ["]", ""]
    LOCK.write_text("\n".join(lines), encoding="utf-8")


def read_lock() -> dict:
    if not LOCK.exists():
        raise Failure(f"no lock file at {LOCK}; run with --update first")
    out = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("{ path ="):
            out[line.split('"')[1]] = line.split('"')[3]
    if not out:
        raise Failure(f"{LOCK} lists no files")
    return out


def update() -> int:
    files = fetch()
    root = DEST / VENDORED_DIR
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    for name, blob in files.items():
        (root / name).write_bytes(blob)
    write_lock({name: hashlib.sha256(blob).hexdigest() for name, blob in files.items()})
    print(f"vendored {len(files)} files into {root.relative_to(REPO)}")
    print(f"wrote {LOCK.relative_to(REPO)}")
    return 0


def check() -> int:
    locked = read_lock()
    problems = []
    for name, digest in sorted(locked.items()):
        path = DEST / name
        if not path.exists():
            problems.append(f"missing: {name}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            problems.append(f"modified: {name}")
    root = DEST / VENDORED_DIR
    on_disk = {p.relative_to(DEST).as_posix() for p in root.rglob("*") if p.is_file()} if root.exists() else set()
    for extra in sorted(on_disk - set(locked)):
        problems.append(f"unlisted: {extra}")
    if problems:
        print(f"{len(problems)} problem(s) against {LOCK.relative_to(REPO)}:", file=sys.stderr)
        for p in problems[:20]:
            print(f"  {p}", file=sys.stderr)
        print("\nRe-run with --update; a change of our own belongs in src/, not in fatfs/.", file=sys.stderr)
        return 1
    print(f"ok: {len(locked)} vendored files match {LOCK.relative_to(REPO)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true", help="fetch the pinned release and patches, re-vendor (network)")
    ap.add_argument("--check", action="store_true", help="verify the working copy against the lock (offline)")
    args = ap.parse_args()
    try:
        return update() if args.update else check()
    except Failure as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
