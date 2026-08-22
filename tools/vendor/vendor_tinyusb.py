#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""libraries/TinyUSB/src is a pinned TinyUSB snapshot.

[ADR-0012](../../docs/adr/0012-usb-stack.ja.md) adopts TinyUSB and keeps it
inside the repository rather than as an external dependency, because we expect
to carry patches - an unmerged upstream PR, or a fix we have not landed yet.

That makes a lock file necessary rather than nice to have. Without one, a local
edit is indistinguishable from upstream code, and the next version bump reverts
it silently. So every vendored file is hashed, and every deliberate difference
from upstream has to be listed as a patch.

  uv run tools/vendor/vendor_tinyusb.py --update      # fetch and re-vendor
  uv run tools/vendor/vendor_tinyusb.py --check       # verify against the lock

--check is offline: it hashes what is on disk and compares. --update needs the
network. Normally reached through `pytest` (tests/vendor/test_vendored_tinyusb.py).

Not all of upstream src/ is vendored: portable/ carries a driver for every
vendor TinyUSB supports, and shipping thirty of them to compile into nothing
helps nobody. The subset is spelled out in KEEP below, and the lock records
exactly what came in.
"""
import argparse
import hashlib
import io
import pathlib
import shutil
import sys
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = REPO / "vendor" / "tinyusb.lock.toml"
DEST = REPO / "libraries" / "TinyUSB" / "src"

TAG = "0.21.0"
URL = f"https://github.com/hathach/tinyusb/archive/refs/tags/{TAG}.tar.gz"

# Which of upstream src/ comes in. Everything outside portable/ is generic and
# small; inside portable/ only the drivers a CH32 build can reach.
#   wch              - the CH32 device (FS/HS) and host (FS) drivers
#   st/stm32_fsdev   - CH32V20x port0 is an ST FSDEV clone and TinyUSB uses
#                      that driver for it
KEEP_PORTABLE = ("portable/wch", "portable/st/stm32_fsdev")

# Files we add next to the vendored tree. They are ours, so the lock ignores
# them - but they must not collide with an upstream path, and a future upstream
# version that adds one of these names has to be noticed, so they are listed.
OURS = ("tusb_config.h", "TinyUSB.h", "TinyUSB.cpp", "ch32_tusb_glue.cpp")


class Failure(Exception):
    pass


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def wanted(name: str) -> bool:
    """True for an upstream src/ path we vendor. `name` is relative to src/."""
    if not name.startswith("portable/"):
        return True
    return any(name == k or name.startswith(k + "/") for k in KEEP_PORTABLE)


def fetch() -> dict:
    """{relative path: bytes} of the subset, straight from the release tarball."""
    print(f"fetching {URL}", flush=True)
    with urllib.request.urlopen(URL, timeout=120) as response:
        blob = response.read()
    out = {}
    prefix = f"tinyusb-{TAG}/src/"
    # The licence sits at the repository root, not in src/, and MIT requires it
    # to travel with the copy - so it comes along as LICENSE inside dest.
    license_path = f"tinyusb-{TAG}/LICENSE"
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tar:
        for member in tar.getmembers():
            if not member.isfile():
                continue
            if member.name == license_path:
                out["LICENSE"] = tar.extractfile(member).read()
                continue
            if not member.name.startswith(prefix):
                continue
            name = member.name[len(prefix):]
            if wanted(name):
                out[name] = tar.extractfile(member).read()
    if "LICENSE" not in out:
        raise Failure(f"no {license_path} in the tarball - refusing to vendor "
                      f"MIT code without its licence")
    if not out:
        raise Failure("the tarball had no src/ files - did the layout change?")
    return out


def write_lock(files: dict) -> None:
    lines = [
        "# TinyUSB vendored snapshot lock",
        "# Regenerate/verify: tools/vendor/vendor_tinyusb.py",
        "# See docs/adr/0012-usb-stack.ja.md",
        "",
        "[[source]]",
        'id = "tinyusb"',
        'url = "https://github.com/hathach/tinyusb.git"',
        f'tag = "{TAG}"',
        'license = "MIT"',
        'license_file = "libraries/TinyUSB/src/LICENSE"',
        f'dest = "{DEST.relative_to(REPO).as_posix()}"',
        "# Only part of upstream src/ is vendored; see KEEP_PORTABLE in the tool.",
        "kept_portable = [" + ", ".join(f'"{k}"' for k in KEEP_PORTABLE) + "]",
        "# Deliberate differences from upstream. Empty means the copy is verbatim.",
        "# Each entry: what it changes and why, so a version bump can re-apply or drop it.",
        "patches = []",
        "# Files this repository adds next to the vendored tree (not upstream).",
        "ours = [" + ", ".join(f'"{o}"' for o in OURS) + "]",
        "",
        "# SHA-256 of every vendored file, relative to dest.",
        "files = [",
    ]
    for name in sorted(files):
        lines.append(f'  {{ path = "{name}", sha256 = "{files[name]}" }},')
    lines += ["]", ""]
    LOCK.write_text("\n".join(lines), encoding="utf-8")


def read_lock() -> dict:
    if not LOCK.exists():
        raise Failure(f"no lock file at {LOCK}; run with --update first")
    out = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("{ path ="):
            path = line.split('"')[1]
            digest = line.split('"')[3]
            out[path] = digest
    if not out:
        raise Failure(f"{LOCK} lists no files")
    return out


def update() -> int:
    files = fetch()
    if DEST.exists():
        shutil.rmtree(DEST)
    hashes = {}
    for name, blob in files.items():
        target = DEST / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
        hashes[name] = hashlib.sha256(blob).hexdigest()
    write_lock(hashes)
    print(f"vendored {len(files)} files into {DEST.relative_to(REPO)}")
    print(f"wrote {LOCK.relative_to(REPO)}")
    return 0


def check() -> int:
    locked = read_lock()
    problems = []

    for name, digest in sorted(locked.items()):
        path = DEST / name
        if not path.exists():
            problems.append(f"missing: {name}")
        elif sha256(path) != digest:
            problems.append(f"modified: {name} (not listed in patches)")

    on_disk = {p.relative_to(DEST).as_posix()
               for p in DEST.rglob("*") if p.is_file()}
    for extra in sorted(on_disk - set(locked) - set(OURS)):
        problems.append(f"unlisted: {extra}")

    if problems:
        print(f"{len(problems)} problem(s) against {LOCK.relative_to(REPO)}:",
              file=sys.stderr)
        for p in problems[:20]:
            print(f"  {p}", file=sys.stderr)
        print("\nIf a difference is deliberate, record it in the lock's "
              "patches list; if not, re-run with --update.", file=sys.stderr)
        return 1

    print(f"ok: {len(locked)} vendored files match {LOCK.relative_to(REPO)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="fetch the pinned tag and re-vendor (needs network)")
    ap.add_argument("--check", action="store_true",
                    help="verify the working copy against the lock (offline)")
    args = ap.parse_args()
    try:
        if args.update:
            return update()
        return check()
    except Failure as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
