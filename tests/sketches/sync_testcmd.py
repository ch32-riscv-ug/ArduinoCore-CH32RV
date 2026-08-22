#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Copy tests/sketches/testcmd.h into every sketch case directory.

  uv run tests/sketches/sync_testcmd.py           # push the original out
  uv run tests/sketches/sync_testcmd.py --check   # CI: fail if a copy drifted

arduino-cli compiles the sketch folder and nothing above it, so a header shared
by every sketch has to *be* in every sketch. The alternatives were worse:

  ../testcmd.h        the preprocessor runs in a build directory the sketch was
                      copied into, so the parent is not the one you meant
  -I via build.extra_flags
                      four different callers build these sketches (pytest-
                      embedded, compile_all, profile_build, smoke) and each
                      would need the same flag; miss one and it fails only
                      there. It also stops the sketch opening in the IDE.
  a symlink per case  needs developer mode on Windows, and CI builds these on
                      windows-latest

So it is copied, and the copy is generated rather than trusted: same shape as
sketch.yaml, whose profiles block sync_profiles.py owns.

The copy carries no edit marker of its own. A header cannot have one that
survives being #included, and a byte-for-byte comparison says more than a
comment does - the file either matches the original or this fails.
"""
import argparse
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE / "testcmd.h"


def cases():
    """Every directory holding a sketch that speaks the protocol.

    A sketch.yaml is what marks one, which also picks up the manual cases -
    gpio_loopback is driven through the same `dut` fixture as the automated
    ones and needs the same header.
    """
    tests = HERE.parent
    return sorted({p.parent for p in HERE.glob("*/*/sketch.yaml")}
                  | {p.parent for p in (tests / "manual").glob("*/sketch.yaml")})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="report drift instead of fixing it")
    args = ap.parse_args()

    want = SOURCE.read_bytes()
    stale = []
    for case in cases():
        target = case / SOURCE.name
        if target.exists() and target.read_bytes() == want:
            continue
        rel = target.relative_to(HERE.parents[1])
        if args.check:
            stale.append(str(rel))
        else:
            shutil.copyfile(SOURCE, target)
            print(f"updated {rel}")

    if stale:
        print("out of date; run: uv run tests/sketches/sync_testcmd.py",
              file=sys.stderr)
        for name in stale:
            print(f"  {name}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
