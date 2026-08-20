#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Every sketch compiles for every board its own sketch.yaml claims.

A profile is a promise that someone can build and flash that sketch on that
board. Nothing checked the promise until this existed, and it was already
broken - three sketches listed a board whose 2 KB of RAM their global String
could never fit in.

  uv run tests/sketches/compile_all.py <workdir>

Normally reached through `pytest` (tests/test_sketch_profiles.py).

Board-specific limits belong in REQUIREMENTS in sync_profiles.py, not here:
this only reports whether the generated profiles are honest.
"""
import argparse
import pathlib
import re
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tests" / "compile"))

from compile_matrix import (Failure, compile_one, gcc_bin,   # noqa: E402
                            link_platform, sandbox)

FQBN = re.compile(r"ch32-riscv-ug:ch32v:[A-Za-z0-9]+:pnum=[A-Za-z0-9]+")
USED = re.compile(r"Sketch uses (\d+) bytes \((\d+)%\)")


def combinations():
    """[(sketch dir, fqbn)] for every board each sketch.yaml names."""
    out = []
    for yaml in sorted(HERE.glob("*/*/sketch.yaml")):
        for fqbn in sorted(set(FQBN.findall(yaml.read_text(encoding="utf-8")))):
            out.append((yaml.parent, fqbn))
    return out


def run(work: pathlib.Path) -> dict:
    work.mkdir(parents=True, exist_ok=True)
    gcc = gcc_bin()
    if shutil.which("arduino-cli") is None:
        raise Failure("arduino-cli is not on PATH")
    env = sandbox(work)
    link_platform(work)

    results, failures = {}, []
    for src, fqbn in combinations():
        name = src.name
        board = fqbn.split(":")[2]
        # Copy the .ino out from under its sketch.yaml: with the profile file
        # present arduino-cli resolves the platform through platform_index_url
        # and ignores --fqbn, so the symlinked working tree would never be
        # built. The copy also proves the sketch needs nothing else from its
        # directory.
        staged = work / "sketches" / name
        staged.mkdir(parents=True, exist_ok=True)
        shutil.copy(src / f"{name}.ino", staged)

        build = work / "build"
        if build.exists():
            shutil.rmtree(build)
        rc, output = compile_one(env, fqbn, gcc, build, staged)
        used = USED.search(output)
        print(f"== {name:16} {board:10} "
              + (used.group(0) if used else ("FAIL" if rc else "ok")), flush=True)
        if rc != 0:
            failures.append((name, board, output.strip().splitlines()[-12:]))
        else:
            results[(name, board)] = int(used.group(1)) if used else None

    print("---", flush=True)
    total = len(results) + len(failures)
    if failures:
        detail = "\n".join(f"  {n} / {b}\n    " + "\n    ".join(t)
                           for n, b, t in failures)
        raise Failure(f"SKETCH PROFILE COMPILE FAILED: {len(failures)} of "
                      f"{total}\n{detail}")
    print(f"SKETCH PROFILE COMPILE OK: {total} combinations", flush=True)
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("workdir", type=pathlib.Path)
    args = ap.parse_args()
    try:
        run(args.workdir)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
