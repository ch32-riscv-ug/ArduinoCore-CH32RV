#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""libraries/NetworkClientSecure/src holds a pinned Mbed TLS release, in part.

docs/network-stack.ja.md picks Mbed TLS 4.1 (the LTS to March 2029) for TLS.
As with every vendored source (docs/vendor-policy.ja.md), each file is hashed
against the release, so a local edit cannot hide among upstream code.

  uv run tools/vendor/vendor_mbedtls.py --update      # fetch and re-vendor
  uv run tools/vendor/vendor_mbedtls.py --check       # verify against the lock

The Arduino build puts only a library's src/ on the include path, and Mbed TLS
4 spreads its sources over Mbed TLS proper and TF-PSA-Crypto, which find each
other's internal headers through -I. So the release is laid out flat:

  include/mbedtls, tf-psa-crypto/include/{mbedtls,psa,tf-psa-crypto},
  tf-psa-crypto/drivers/builtin/include/mbedtls      -> src/mbedtls/, src/psa/, src/tf-psa-crypto/
  library/, tf-psa-crypto/{core,drivers/builtin/src,platform,utilities,
  dispatch,extras}/ (*.c, *.h)                        -> src/mbedtls-src/
  the two configuration checks (*_config.c)           -> extras/mbedtls-config-check/

(no two of those files share a name; the two cross-directory includes left
are behind options this configuration leaves off). The two default
configuration headers, mbedtls/mbedtls_config.h and psa/crypto_config.h, are
not taken: ours replace them. No programs, tests, 3rdparty or the optional
drivers (everest, p256-m, pqcp).
"""
import argparse
import hashlib
import io
import pathlib
import sys
import tarfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = REPO / "vendor" / "mbedtls.lock.toml"
DEST = REPO / "libraries" / "NetworkClientSecure"

VERSION = "4.1.1"
TAG = f"mbedtls-{VERSION}"
URL = f"https://github.com/Mbed-TLS/mbedtls/releases/download/{TAG}/{TAG}.tar.bz2"
ARCHIVE_SHA256 = "3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c"
PREFIX = f"{TAG}/"

PUBLIC_ROOTS = ("include/", "tf-psa-crypto/include/", "tf-psa-crypto/drivers/builtin/include/")
INTERNAL_DIRS = ("library/", "tf-psa-crypto/core/", "tf-psa-crypto/drivers/builtin/src/", "tf-psa-crypto/platform/",
                 "tf-psa-crypto/utilities/", "tf-psa-crypto/dispatch/", "tf-psa-crypto/extras/")
REPLACED = ("src/mbedtls/mbedtls_config.h", "src/psa/crypto_config.h")   # ours, next to the vendored headers
VENDORED_DIRS = ("src/mbedtls", "src/psa", "src/tf-psa-crypto", "src/mbedtls-src", "extras/mbedtls-config-check")
# The configuration checks: compile-time only, and they include their headers
# through a macro that only -I resolves, so not under src/ (which the Arduino
# build compiles); the host test compiles them with -I src/mbedtls-src.
CONFIG_CHECKS = ("library/mbedtls_config.c", "tf-psa-crypto/core/tf_psa_crypto_config.c")
PATCHES = ()


class Failure(Exception):
    pass


def mapped(name: str):
    """The DEST-relative path of a release path (relative to the release root), or None."""
    if name == "LICENSE":
        return "src/mbedtls-src/LICENSE"
    if name in CONFIG_CHECKS:
        return "extras/mbedtls-config-check/" + name.rsplit("/", 1)[1]
    for root in PUBLIC_ROOTS:
        if name.startswith(root) and name.endswith(".h"):
            rest = "src/" + name[len(root):]
            if rest.split("/")[1] in ("mbedtls", "psa", "tf-psa-crypto") and rest not in REPLACED:
                return rest
            return None
    for d in INTERNAL_DIRS:
        if name.startswith(d) and "/" not in name[len(d):] and name.endswith((".c", ".h")):
            return "src/mbedtls-src/" + name[len(d):]
    return None


def fetch() -> dict:
    print(f"fetching {URL}", flush=True)
    with urllib.request.urlopen(URL, timeout=300) as response:
        blob = response.read()
    digest = hashlib.sha256(blob).hexdigest()
    if digest != ARCHIVE_SHA256:
        raise Failure(f"archive sha256 {digest} is not the pinned {ARCHIVE_SHA256}")
    out = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:bz2") as tar:
        for member in tar.getmembers():
            if not member.isfile() or not member.name.startswith(PREFIX):
                continue
            target = mapped(member.name[len(PREFIX):])
            if target:
                if target in out:
                    raise Failure(f"two release files map to {target}")
                out[target] = tar.extractfile(member).read()
    if "src/mbedtls-src/LICENSE" not in out:
        raise Failure("no LICENSE in the archive - refusing to vendor without the licence")
    return out


def write_lock(files: dict) -> None:
    lines = [
        "# Mbed TLS vendored snapshot lock",
        "# Regenerate/verify: tools/vendor/vendor_mbedtls.py",
        "# See docs/network-stack.ja.md",
        "",
        "[[source]]",
        'id = "mbedtls"',
        'url = "https://github.com/Mbed-TLS/mbedtls.git"',
        f'tag = "{TAG}"',
        f'archive = "{URL}"',
        f'archive_sha256 = "{ARCHIVE_SHA256}"',
        'license = "Apache-2.0 OR GPL-2.0-or-later (used under Apache-2.0)"',
        'license_file = "libraries/NetworkClientSecure/src/mbedtls-src/LICENSE"',
        f'dest = "{DEST.relative_to(REPO).as_posix()}"',
        "# Only these directories under dest are upstream; the rest of dest is the NetworkClientSecure library.",
        "dirs = [" + ", ".join(f'"{d}"' for d in VENDORED_DIRS) + "]",
        "# Our configuration headers in place of the release's defaults (not upstream files).",
        "ours = [" + ", ".join(f'"{o}"' for o in REPLACED) + "]",
        "# Paths are remapped from the release layout; see the tool's docstring.",
        "# Deliberate differences from upstream. Empty means the copy is verbatim.",
        "patches = []",
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
        root = DEST / d
        if root.exists():
            for p in sorted(root.rglob("*"), reverse=True):
                rel = p.relative_to(DEST).as_posix()
                if p.is_file() and rel not in REPLACED:
                    p.unlink()
                elif p.is_dir() and not any(p.iterdir()):
                    p.rmdir()
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
    for extra in sorted(vendored_on_disk() - set(locked) - set(REPLACED)):
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
