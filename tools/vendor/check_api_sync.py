#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""cores/arduino/api is the pinned ArduinoCore-API, byte for byte.

ADR-0009 vendors a snapshot rather than a submodule. That only stays honest if
something compares it against the commit the lock file names - a local edit to
a vendored file is invisible until an upstream bump silently reverts it.

  uv run tools/vendor/check_api_sync.py <workdir>

Normally reached through `pytest` (tests/test_vendored_api.py).

Four things are checked, and they catch different mistakes:
  1. the pinned commit really carries the pinned api/ tree  (a bad pin)
  2. our copy matches that tree exactly                     (a local edit)
  3. every file is in the lock with a matching hash         (an unlisted file)
  4. ARDUINO_API_VERSION matches the pin                    (a mismatched bump)
"""
import argparse
import filecmp
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = REPO / "vendor" / "arduino-core-api.lock.toml"
DEST = REPO / "cores" / "arduino" / "api"


class Failure(Exception):
    pass


def git(*args, cwd=None) -> str:
    proc = subprocess.run(["git", *args], cwd=cwd, capture_output=True,
                          text=True)
    if proc.returncode != 0:
        raise Failure(f"git {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout.strip()


def lock_value(text: str, key: str) -> str:
    m = re.search(rf'^{key} = "([^"]*)"', text, re.M)
    if m is None:
        raise Failure(f"{LOCK.name} has no {key}")
    return m.group(1)


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_trees(upstream: pathlib.Path, ours: pathlib.Path) -> list:
    """Every difference between two directory trees, as readable lines."""
    problems = []

    def walk(cmp_result, prefix=""):
        for name in cmp_result.left_only:
            problems.append(f"missing from our copy: {prefix}{name}")
        for name in cmp_result.right_only:
            problems.append(f"not in upstream: {prefix}{name}")
        for name in cmp_result.diff_files:
            problems.append(f"differs: {prefix}{name}")
        for name, sub in cmp_result.subdirs.items():
            walk(sub, f"{prefix}{name}/")

    walk(filecmp.dircmp(str(upstream), str(ours)))
    return problems


def run(work: pathlib.Path) -> dict:
    work.mkdir(parents=True, exist_ok=True)
    text = LOCK.read_text(encoding="utf-8")
    url = lock_value(text, "url")
    tag = lock_value(text, "tag")
    commit = lock_value(text, "commit")
    tree = lock_value(text, "api_tree_sha1")
    m = re.search(r"^arduino_api_version = (\d+)", text, re.M)
    if m is None:
        raise Failure("the lock has no arduino_api_version")
    api_version = m.group(1)
    print(f"pinned: {url} @ {tag} ({commit}), api tree {tree}", flush=True)

    up = work / "ArduinoCore-API"
    if not (up / ".git").is_dir():
        git("clone", "--quiet", "--filter=blob:none", "--no-checkout", url,
            str(up))
    git("fetch", "--quiet", "--depth", "1", "origin", commit, cwd=up)
    git("checkout", "--quiet", "--detach", commit, cwd=up)

    # 1) the pinned commit really carries the pinned api/ tree
    actual = git("rev-parse", f"{commit}^{{tree}}:api", cwd=up)
    if actual != tree:
        raise Failure(f"upstream api/ tree {actual} != pinned {tree}")

    # 2) our copy is byte-identical. LICENSE is upstream's root one, which the
    #    import places inside api/, so put it where the comparison expects.
    shutil.copy(up / "LICENSE", up / "api" / "LICENSE")
    differences = compare_trees(up / "api", DEST)
    if differences:
        raise Failure(f"cores/arduino/api differs from upstream {tag}:\n  "
                      + "\n  ".join(differences))

    # 3) every file is listed in the lock with a matching hash
    entries = re.findall(r'\{ path = "([^"]+)", sha256 = "([0-9a-f]+)"', text)
    if not entries:
        raise Failure("the lock lists no files")
    mismatched = []
    for path, want in entries:
        f = DEST / path
        if not f.is_file():
            mismatched.append(f"{path}: listed in the lock but not present")
        elif sha256(f) != want:
            mismatched.append(f"{path}: sha256 mismatch")
    present = sum(1 for p in DEST.rglob("*") if p.is_file())
    if len(entries) != present:
        mismatched.append(f"the lock lists {len(entries)} files, the tree has "
                          f"{present}")
    if mismatched:
        raise Failure("\n  ".join(["lock does not describe the tree:"] + mismatched))

    # 4) the API version guard matches the pin
    header = (DEST / "ArduinoAPI.h").read_text(encoding="utf-8")
    if not re.search(rf"^#define ARDUINO_API_VERSION {api_version}$", header, re.M):
        raise Failure(f"ArduinoAPI.h does not define ARDUINO_API_VERSION "
                      f"{api_version}")

    print(f"API SYNC OK ({len(entries)} files, {tag}, "
          f"ARDUINO_API_VERSION {api_version})", flush=True)
    return {"files": len(entries), "tag": tag, "commit": commit,
            "api_version": int(api_version)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", nargs="?", type=pathlib.Path,
                    default=pathlib.Path("/tmp/api-sync"))
    args = ap.parse_args()
    try:
        run(args.workdir)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
