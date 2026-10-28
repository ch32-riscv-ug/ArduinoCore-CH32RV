#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""cores/arduino/api is the pinned ArduinoCore-API, byte for byte.

ADR-0009 vendors a snapshot rather than a submodule. That only stays honest if
something compares it against the commit the lock file names - a local edit to
a vendored file is invisible until an upstream bump silently reverts it.

  uv run tools/vendor/check_api_sync.py <workdir>

The upstream comparison is run directly by CI. Offline hash checks live in
tests/platform/test_source_locks.py.

Four things are checked, and they catch different mistakes:
  1. the pinned commit really carries the pinned api/ tree  (a bad pin)
  2. our copy matches that tree plus PATCHES exactly        (a local edit)
  3. every file is in the lock with its upstream hash       (an unlisted file)
  4. ARDUINO_API_VERSION matches the pin                    (a mismatched bump)

PATCHES are the deliberate differences (ADR-0009): each replaces one piece of
upstream text, once. The lock keeps upstream's hashes; unpatched() gives the
upstream bytes back (tests/platform/test_source_locks.py uses it too).
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


# Print::printFloat hands over to the core (cores/arduino/ch32rv_printf.cpp),
# which computes in float or double as the Floating Point menu says and shares
# its fixed-point text with printf's %f and dtostrf.
PATCHES = (
    {
        "path": "Print.cpp",
        "before": "size_t Print::printFloat(double number, int digits)\n{\n" + '  if (digits < 0)\n    digits = 2;\n\n  size_t n = 0;\n\n  if (isnan(number)) return print("nan");\n  if (isinf(number)) return print("inf");\n  if (number > 4294967040.0) return print ("ovf");  // constant determined empirically\n  if (number <-4294967040.0) return print ("ovf");  // constant determined empirically\n\n  // Handle negative numbers\n  if (number < 0.0)\n  {\n     n += print(\'-\');\n     number = -number;\n  }\n\n  // Round correctly so that print(1.999, 2) prints as "2.00"\n  double rounding = 0.5;\n  for (uint8_t i=0; i<digits; ++i)\n    rounding /= 10.0;\n\n  number += rounding;\n\n  // Extract the integer part of the number and print it\n  unsigned long int_part = (unsigned long)number;\n  double remainder = number - (double)int_part;\n  n += print(int_part);\n\n  // Print the decimal point, but only if there are digits beyond\n  if (digits > 0) {\n    n += print(".");\n  }\n\n  // Extract digits from the remainder one at a time\n  while (digits-- > 0)\n  {\n    remainder *= 10.0;\n    unsigned int toPrint = (unsigned int)remainder;\n    n += print(toPrint);\n    remainder -= toPrint;\n  }\n\n  return n;\n}\n',
        "after": "size_t Print::printFloat(double number, int digits)\n{\n" + "  /* ch32rv patch: the core's fixed-point text, in float or double as the\n   * Floating Point menu says (tools/vendor/check_api_sync.py). */\n  extern size_t ch32rv_print_float(Print &, double, int);\n  return ch32rv_print_float(*this, number, digits);\n}\n",
        "why": ("Print::printFloat in float or double as the Floating Point menu says, sharing the "
                "core's fixed-point text (ch32rv_format.h) with printf's %f and dtostrf"),
    },
)


class Failure(Exception):
    pass


def apply_patches(root: pathlib.Path) -> None:
    """Upstream's tree under root, patched in place."""
    for p in PATCHES:
        f = root / p["path"]
        text = f.read_text(encoding="utf-8")
        if text.count(p["before"]) != 1:
            raise Failure(f"patch no longer applies to {p['path']}: {p['why']}")
        f.write_text(text.replace(p["before"], p["after"], 1), encoding="utf-8")


def unpatched(name: str, blob: bytes):
    """The upstream bytes of a patched file, or None when a patch is missing."""
    text = blob.decode("utf-8")
    for p in reversed([p for p in PATCHES if p["path"] == name]):
        if text.count(p["after"]) != 1:
            return None
        text = text.replace(p["after"], p["before"], 1)
    return text.encode("utf-8")


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
    git("checkout", "--quiet", "--force", "--detach", commit, cwd=up)

    # 1) the pinned commit really carries the pinned api/ tree
    actual = git("rev-parse", f"{commit}^{{tree}}:api", cwd=up)
    if actual != tree:
        raise Failure(f"upstream api/ tree {actual} != pinned {tree}")

    # 2) our copy is byte-identical to upstream with PATCHES. LICENSE is
    #    upstream's root one, which the import places inside api/, so put it
    #    where the comparison expects.
    shutil.copy(up / "LICENSE", up / "api" / "LICENSE")
    apply_patches(up / "api")
    differences = compare_trees(up / "api", DEST)
    if differences:
        raise Failure(f"cores/arduino/api differs from upstream {tag}:\n  "
                      + "\n  ".join(differences))

    # 3) every file is listed in the lock with upstream's hash
    entries = re.findall(r'\{ path = "([^"]+)", sha256 = "([0-9a-f]+)"', text)
    if not entries:
        raise Failure("the lock lists no files")
    mismatched = []
    for path, want in entries:
        f = DEST / path
        if not f.is_file():
            mismatched.append(f"{path}: listed in the lock but not present")
        else:
            data = f.read_bytes()
            if any(p["path"] == path for p in PATCHES):
                data = unpatched(path, data)
                if data is None:
                    mismatched.append(f"{path}: patch missing")
                    continue
            if hashlib.sha256(data).hexdigest() != want:
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
