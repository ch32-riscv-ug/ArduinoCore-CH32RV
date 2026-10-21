#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""libraries/Network/src holds a pinned lwIP release, in part.

docs/network-stack.ja.md adopts lwIP (NO_SYS, raw API) as the IP stack of the
Network library. As with any vendored source (docs/vendor-policy.ja.md), every
file is hashed against the release so a local edit cannot hide among upstream
code, and every deliberate difference has to be listed as a patch.

  uv run tools/vendor/vendor_lwip.py --update      # fetch and re-vendor
  uv run tools/vendor/vendor_lwip.py --check       # verify against the lock

--check is offline. --update needs the network. Offline integrity checks also
live in tests/platform/test_source_locks.py.

The Arduino build compiles every source under a library's src/ and puts only
src/ itself on the include path, so the release's layout is remapped:

  src/include/lwip/...   -> lwip/...        (#include "lwip/opt.h" resolves)
  src/include/netif/...  -> netif/...
  src/core/..., the Ethernet netif, COPYING -> lwip-src/...

Only what a NO_SYS, raw-API build can reach comes in: no sockets / netconn
(they need an OS), no PPP, 6LoWPAN, SLIP or the other netifs, and no apps yet.
lwipopts.h and arch/ are ours, next to the vendored directories.
"""
import argparse
import hashlib
import io
import pathlib
import shutil
import sys
import urllib.request
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = REPO / "vendor" / "lwip.lock.toml"
DEST = REPO / "libraries" / "Network" / "src"

VERSION = "2.2.1"
TAG = "STABLE-2_2_1_RELEASE"
URL = f"https://download.savannah.nongnu.org/releases/lwip/lwip-{VERSION}.zip"
ARCHIVE_SHA256 = "7b622662dba2383d71f874f2e494b54ae948531559c17acbe75797966d646878"
PREFIX = f"lwip-{VERSION}/"

# The vendored directories under DEST. Everything in them is upstream; our own
# files live outside them.
VENDORED_DIRS = ("lwip", "netif", "lwip-src")

# Deliberate differences from upstream, applied by --update and verified by
# --check (see vendor_tinyusb.py in Git history for the scheme). None so far.
PATCHES = ()


class Failure(Exception):
    pass


def mapped(name: str):
    """The DEST-relative path of an upstream path, or None when it is left out.
    `name` is relative to the release root."""
    if name == "COPYING":
        return "lwip-src/COPYING"
    if name.startswith("src/include/lwip/"):
        rest = name[len("src/include/lwip/"):]
        if rest.startswith("apps/"):
            return None
        return "lwip/" + rest
    if name in ("src/include/netif/ethernet.h", "src/include/netif/etharp.h"):
        return name[len("src/include/"):]
    if name.startswith("src/core/") and name.endswith(".c"):
        return "lwip-src/" + name[len("src/"):]
    if name == "src/netif/ethernet.c":
        return "lwip-src/netif/ethernet.c"
    return None


def fetch() -> dict:
    """{DEST-relative path: bytes} from the release archive."""
    print(f"fetching {URL}", flush=True)
    with urllib.request.urlopen(URL, timeout=120) as response:
        blob = response.read()
    digest = hashlib.sha256(blob).hexdigest()
    if digest != ARCHIVE_SHA256:
        raise Failure(f"archive sha256 {digest} is not the pinned {ARCHIVE_SHA256}")
    out = {}
    with zipfile.ZipFile(io.BytesIO(blob)) as z:
        for info in z.infolist():
            if info.is_dir() or not info.filename.startswith(PREFIX):
                continue
            target = mapped(info.filename[len(PREFIX):])
            if target:
                out[target] = z.read(info)
    if "lwip-src/COPYING" not in out:
        raise Failure("no COPYING in the archive - refusing to vendor without the licence")
    if not any(k.startswith("lwip-src/core/") for k in out):
        raise Failure("the archive had no src/core - did the layout change?")
    return out


def write_lock(files: dict) -> None:
    lines = [
        "# lwIP vendored snapshot lock",
        "# Regenerate/verify: tools/vendor/vendor_lwip.py",
        "# See docs/network-stack.ja.md",
        "",
        "[[source]]",
        'id = "lwip"',
        'url = "https://git.savannah.nongnu.org/git/lwip.git"',
        f'tag = "{TAG}"',
        f'archive = "{URL}"',
        f'archive_sha256 = "{ARCHIVE_SHA256}"',
        'license = "BSD-3-Clause"',
        'license_file = "libraries/Network/src/lwip-src/COPYING"',
        f'dest = "{DEST.relative_to(REPO).as_posix()}"',
        "# Only these directories under dest are upstream; the rest of dest is the Network library.",
        "dirs = [" + ", ".join(f'"{d}"' for d in VENDORED_DIRS) + "]",
        "# Paths are remapped from the release layout; see the tool's docstring.",
        "# Deliberate differences from upstream. Empty means the copy is verbatim.",
        "patches = [" + ", ".join(f'"{p["path"]}: {p["why"][:60]}"' for p in PATCHES) + "]",
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
            out[line.split('"')[1]] = line.split('"')[3]
    if not out:
        raise Failure(f"{LOCK} lists no files")
    return out


def vendored_on_disk() -> set:
    found = set()
    for d in VENDORED_DIRS:
        root = DEST / d
        if root.exists():
            found |= {p.relative_to(DEST).as_posix() for p in root.rglob("*") if p.is_file()}
    return found


def update() -> int:
    files = fetch()
    for d in VENDORED_DIRS:
        if (DEST / d).exists():
            shutil.rmtree(DEST / d)
    hashes = {name: hashlib.sha256(blob).hexdigest() for name, blob in files.items()}
    for name, blob in files.items():
        target = DEST / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
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
        elif hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            problems.append(f"modified: {name} (not listed in patches)")
    for extra in sorted(vendored_on_disk() - set(locked)):
        problems.append(f"unlisted: {extra}")
    if problems:
        print(f"{len(problems)} problem(s) against {LOCK.relative_to(REPO)}:", file=sys.stderr)
        for p in problems[:20]:
            print(f"  {p}", file=sys.stderr)
        print("\nIf a difference is deliberate, record it as a patch; if not, re-run with --update.",
              file=sys.stderr)
        return 1
    print(f"ok: {len(locked)} vendored files match {LOCK.relative_to(REPO)}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="fetch the pinned release and re-vendor (needs network)")
    ap.add_argument("--check", action="store_true",
                    help="verify the working copy against the lock (offline)")
    args = ap.parse_args()
    try:
        return update() if args.update else check()
    except Failure as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
