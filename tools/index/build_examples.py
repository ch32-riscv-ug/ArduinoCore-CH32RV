#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Build every bundled example for the profiles its sketch.yaml declares,
against this working tree.

  uv run tools/index/build_examples.py <workdir> [--profile ch32x035 ...]
  uv run tools/index/build_examples.py --list-profiles

The platform is the working tree itself: <workdir>/user/hardware/<packager>/
<arch> holds a link to each entry a release archive carries (gen_index.py's
PLATFORM_ENTRIES), so nothing is packaged or installed - install_check.py is
the test of that. Each example is copied without its sketch.yaml (which pins
a released platform) and built with `-b` and the profile's FQBN. The compiler
comes from the pinned toolchain (fetch_tools.py --tool xpack-riscv-none-elf-gcc).

Every build is `--clean`: no cached core or sketch from an earlier build
counts as checked (it costs little - the core is rebuilt in seconds). Builds
run one after another; CI runs one job per profile. Exit status 1 when
any build fails; each failure is printed with its first error line (as a
GitHub annotation under Actions).
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

from fetch_tools import DEFAULT_ROOT, env_defaults   # noqa: E402
from gen_index import PLATFORM_ENTRIES               # noqa: E402

PACKAGER = "ch32-riscv-ug"
ARCH = "ch32rv"
PROFILE = re.compile(rf"^  (\w+):\n    fqbn: {PACKAGER}:{ARCH}:(\S+)", re.M)


def examples() -> list:
    """[(example dir, {profile: fqbn tail})], in path order."""
    out = []
    for yml in sorted(REPO.glob("libraries/*/examples/*/sketch.yaml")):
        out.append((yml.parent, dict(PROFILE.findall(yml.read_text()))))
    return out


def platform(work: pathlib.Path) -> pathlib.Path:
    """A sketchbook whose hardware/ links to the working tree."""
    user = work / "user"
    dest = user / "hardware" / PACKAGER / ARCH
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    for entry in PLATFORM_ENTRIES:
        if (REPO / entry).exists():
            (dest / entry).symlink_to(REPO / entry)
    return user


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("workdir", nargs="?", type=pathlib.Path)
    ap.add_argument("--profile", action="append", help="only these profiles (default: all)")
    ap.add_argument("--list-profiles", action="store_true",
                    help="print the profiles the examples declare, as a JSON list, and exit")
    args = ap.parse_args()

    found = examples()
    profiles = sorted({p for _, ps in found for p in ps})
    if args.list_profiles:
        print("[" + ", ".join(f'"{p}"' for p in profiles) + "]")
        return 0
    if args.workdir is None:
        ap.error("workdir is required")
    unknown = set(args.profile or ()) - set(profiles)
    if unknown:
        ap.error(f"no example declares {sorted(unknown)}")
    wanted = set(args.profile or profiles)

    gcc = pathlib.Path(env_defaults(DEFAULT_ROOT)["CH32_GCC_BIN"])
    if not (gcc / "riscv-none-elf-gcc").exists() and not (gcc / "riscv-none-elf-gcc.exe").exists():
        print(f"no toolchain at {gcc}: run tools/index/fetch_tools.py --tool xpack-riscv-none-elf-gcc",
              file=sys.stderr)
        return 2
    if shutil.which("arduino-cli") is None:
        print("arduino-cli is not on PATH", file=sys.stderr)
        return 2

    work = args.workdir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ,
               ARDUINO_DIRECTORIES_USER=str(platform(work)),
               ARDUINO_DIRECTORIES_DATA=str(work / "data"),
               ARDUINO_DIRECTORIES_DOWNLOADS=str(work / "downloads"))
    annotate = os.environ.get("GITHUB_ACTIONS") == "true"

    built = failed = 0
    started = time.monotonic()
    for example, ps in found:
        todo = [(p, f) for p, f in ps.items() if p in wanted]
        if not todo:
            continue
        copy = work / "sketch" / example.name
        shutil.rmtree(copy.parent, ignore_errors=True)
        shutil.copytree(example, copy, ignore=shutil.ignore_patterns("sketch.yaml", "build"))
        rel = example.relative_to(REPO)
        for profile, fqbn in todo:
            proc = subprocess.run(
                ["arduino-cli", "compile", "--clean", "-b", f"{PACKAGER}:{ARCH}:{fqbn}",
                 "--build-property", f"compiler.path={gcc}{os.sep}",
                 "--build-path", str(work / "build" / profile), str(copy)],
                env=env, text=True, capture_output=True)
            built += 1
            if proc.returncode == 0:
                print(f"ok   {profile:20} {rel}", flush=True)
                continue
            failed += 1
            lines = (proc.stdout + proc.stderr).splitlines()
            first = next((l for l in lines if re.search(r"error|overflowed", l, re.I)), lines[-1] if lines else "?")
            print(f"FAIL {profile:20} {rel}: {first}", flush=True)
            if annotate:
                print(f"::error file={rel.as_posix()}/{example.name}.ino::{profile}: {first}", flush=True)
    print(f"{built - failed} of {built} builds passed in {time.monotonic() - started:.0f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
