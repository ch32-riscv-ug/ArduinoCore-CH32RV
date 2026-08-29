#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Every bundled example still compiles.

Examples rot faster than anything else in a core: they are the only code that
is not exercised by a test, and they are the first thing a new user runs. This
builds all of them, so an API change that leaves an example behind fails here
instead of in someone's IDE.

Two board sets, because they answer different questions
(docs/examples-build-rules.ja.md):

  fast   CH32X035 and CH32V003 - the main target (24-bit ports, 48 MHz) and
         the floor (rv32ec, 16 KB flash, 2 KB RAM). Runs before a commit.
  sweep  every series. Slow, and meant for GitHub Actions, where the cost is
         wall-clock nobody is watching.

Which boards an example is built for is the example's own declaration, not a
list kept here. An example that needs a peripheral or more flash than some
series has says so in its own header:

    /* requires: USBFS */
    /* requires: USBPD, flash=32K */

The capability names are the <X> of CH32_CLKEN_<X>_ADDR in the generated
variant header, so the question "does this series have a USB device
controller?" is answered by device-data rather than by a hand-written table.
An example with no `requires:` line is built for every board.

  uv run tests/compile/compile_examples.py <workdir> [--sweep]

Normally reached through `pytest` (tests/compile/test_examples.py).
"""
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "tests"))
sys.path.insert(0, str(REPO / "tests" / "sketches"))

from compile_matrix import (Failure, compile_one, gcc_bin,   # noqa: E402
                            link_platform, sandbox)
from sketch_requirements import (BadRequirement, all_boards,   # noqa: E402
                                 requirements, unmet)
from stage import stage_sketch                                # noqa: E402

FAST_BOARDS = ("CH32X035", "CH32V003")

# Build properties an example needs to compile at all. This is not a skip: the
# example is built, with the same flag its own comment tells a user to pass.
#
# Blink is the case that exists: a Generic board is a silicon series, not a
# PCB, so it defines no LED_BUILTIN and Blink refuses to guess a pad
# (docs/board-layer-rules.ja.md). Building it the documented way is what keeps
# the documented way honest - if the flag stops working, this fails.
EXTRA_PROPERTIES = {
    ("CH32", "Blink"): ["build.extra_flags=-DLED_BUILTIN=PA1"],
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


def run(work: pathlib.Path, boards=FAST_BOARDS) -> dict:
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
    failures = []
    for library, name, src in found:
        try:
            need = requirements(src)
        except BadRequirement as e:
            raise Failure(str(e)) from None
        # Build a copy without sketch.yaml. With the profile file present,
        # arduino-cli resolves the platform through its platform_index_url and
        # ignores --fqbn, so this would silently build against the published
        # index instead of the working tree under test. Same reason
        # tests/sketches stages its cases - see tests/sketches/stage.py.
        staged = stage_sketch(src, work / "src" / library / name)
        done = []
        for board in boards:
            reason = unmet(need, board)
            if reason:
                skipped.append((library, name, board, reason))
                continue
            fqbn = f"ch32-riscv-ug:ch32v:{board}:pnum=ANY"
            build = work / "build" / board / library / name
            properties = EXTRA_PROPERTIES.get((library, name), ())
            code, output = compile_one(env, fqbn, gcc, build, staged,
                                       properties)
            if code:
                # Keep going. This run is twenty minutes long and its reader is
                # a failure notification, so stopping at the first break would
                # cost another twenty minutes per fix. Everything needed to act
                # on it is in the block: which example, which board, what it
                # said it needed, and what it was built with.
                declared = ", ".join(need["caps"]) or "nothing"
                failures.append(
                    f"{library}/{name} does not compile for {board}\n"
                    f"  declared requires: caps={declared} "
                    f"flash={need['flash']} ram={need['ram']}\n"
                    f"  build properties: {list(properties) or 'none'}\n"
                    + "\n".join("  " + line
                                for line in output.strip().splitlines()))
                continue
            built.append((library, name, board))
            done.append(board)
        print(f"== {library}/{name}: ok on {len(done)}/{len(boards)}"
              f"{'  (' + ', '.join(done) + ')' if len(boards) <= 4 else ''}",
              flush=True)

    for library, name, board, reason in skipped:
        print(f"== {library}/{name}: skipped on {board} - {reason}", flush=True)
    if failures:
        raise Failure(f"{len(failures)} example builds failed:\n\n"
                      + "\n\n".join(failures))
    print(f"EXAMPLES OK: {len(built)} builds from {len(found)} examples "
          f"x {len(boards)} boards, {len(skipped)} skipped")
    return {"examples": found, "built": built, "skipped": skipped,
            "boards": boards}


def main() -> int:
    args = [a for a in sys.argv[1:]]
    sweep = "--sweep" in args
    args = [a for a in args if a != "--sweep"]
    if len(args) != 1:
        print(__doc__)
        return 2
    try:
        run(pathlib.Path(args[0]).resolve(),
            all_boards() if sweep else FAST_BOARDS)
    except Failure as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
