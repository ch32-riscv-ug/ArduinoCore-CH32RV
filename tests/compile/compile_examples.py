#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Every bundled example still compiles.

Examples rot faster than anything else in a core: they are the only code that
is not exercised by a test, and they are the first thing a new user runs. This
builds all of them, so an API change that leaves an example behind fails here
instead of in someone's IDE.

Two boards rather than all of them: CH32X035 is the main target (24-bit ports,
48 MHz) and CH32V003 is the floor (rv32ec, 16 KB flash, 2 KB RAM). An example
that fits both fits everything in between, and the pair catches the two
mistakes that actually happen - using an API the small part cannot afford, and
using a pad encoding only the wide-port part has.

  uv run tests/compile/compile_examples.py <workdir>

Normally reached through `pytest` (tests/compile/test_examples.py).
"""
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

from compile_matrix import (Failure, compile_one, gcc_bin,   # noqa: E402
                            link_platform, sandbox)

BOARDS = ("CH32X035", "CH32V003")

# An example that cannot fit the floor board says so here, with the reason.
# Trimming the example until it fits would make it worse for every other board,
# and pretending it fits would be a lie the linker catches later. Skips are
# printed, never silent - the same rule sync_profiles.py follows for sketches.
SKIP = {
    ("CH32", "PrintFormatting"): {
        "CH32V003": "dtostrf() pulls the full formatter with float support, "
                    "which does not fit 16 KB",
    },
}


def examples() -> list:
    """[(library, name, directory)] for every bundled example, sorted."""
    out = []
    for ino in sorted((REPO / "libraries").glob("*/examples/*/*.ino")):
        # arduino-cli requires the .ino to be named after its directory.
        if ino.stem != ino.parent.name:
            raise Failure(f"{ino.relative_to(REPO)} must be named "
                          f"{ino.parent.name}.ino")
        out.append((ino.parents[2].name, ino.parent.name, ino.parent))
    return out


def run(work: pathlib.Path) -> dict:
    if shutil.which("arduino-cli") is None:
        raise Failure("arduino-cli is not on PATH")
    work.mkdir(parents=True, exist_ok=True)
    env = sandbox(work)
    link_platform(work)
    gcc = gcc_bin()

    found = examples()
    if not found:
        raise Failure("no examples under libraries/*/examples")

    built = []
    skipped = []
    for library, name, src in found:
        done = []
        for board in BOARDS:
            reason = SKIP.get((library, name), {}).get(board)
            if reason:
                skipped.append((library, name, board, reason))
                continue
            fqbn = f"ch32-riscv-ug:ch32v:{board}:pnum=ANY"
            build = work / "build" / board / library / name
            code, output = compile_one(env, fqbn, gcc, build, src)
            if code:
                raise Failure(f"{library}/{name} does not compile for "
                              f"{board}:\n{output}")
            built.append((library, name, board))
            done.append(board)
        print(f"== {library}/{name}: ok on {', '.join(done)}", flush=True)

    for library, name, board, reason in skipped:
        print(f"== {library}/{name}: skipped on {board} - {reason}", flush=True)
    print(f"EXAMPLES OK: {len(built)} builds from {len(found)} examples "
          f"x {len(BOARDS)} boards, {len(skipped)} skipped")
    return {"examples": found, "built": built, "skipped": skipped}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    try:
        run(pathlib.Path(sys.argv[1]).resolve())
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
