"""
Purpose:
    Prove that our own crt0 hands setup() a correctly initialised RAM on this
    board. ADR-0003 replaced WCH's startup with our own, and until CH32V003 was
    run in experiment 0010 the only evidence was ELF equivalence against the EVT
    startup - a static check. This is the dynamic one, and unlike the experiment
    it is repeatable on any board, which is the point: the linker script and the
    vector table differ per variant.

Why manual:
    It needs the board on the bench and a probe that can write RAM.

Required hardware:
    - One CH32 board with a WCH-LinkE attached (flash + Serial over one cable)
    - No wiring beyond what smoke already needs

Method:
    RAM is filled with a pattern *after* flashing and *before* reset, then the
    sketch reports what a C++ global constructor saw. Filling first is what
    makes the result mean anything: "bss was zero" proves nothing on a part
    whose RAM powers up at zero, so the same run also checks that the pattern
    is still sitting in the word past _ebss, which nothing initialises.

    Ordering matters and is easy to get backwards - the flash algorithm itself
    uses RAM, so the fill has to come after the upload, not before.

Setup:
    cd tests
    uv run --env-file .env pytest manual/crt0_probe/crt0_probe.py -v -s

The sketch sits in sketch/ rather than beside this file because a *.ino in a
test directory is how pytest-embedded recognises one of its own sketch tests,
and it then demands a sketch.yaml. This one is material the driver copies and
builds itself, so that it can fill RAM between the upload and the reset.
"""
import pathlib
import re
import subprocess
import sys
import tempfile
import time

import pytest

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "tests" / "manual" / "smoke"))
sys.path.insert(0, str(REPO / "tests" / "sketches"))

import smoke                                                    # noqa: E402
from stage import stage_sketch                                  # noqa: E402

PATTERN = 0xDEADBEEF
# How long the banner may take to appear after `probe-rs reset`, and how long
# the report may take once RUN is sent. Both are generous: this test runs once
# and a tight bound only buys a flaky failure. Ten seconds for the report was
# tried and CH32V103 missed it - the WCH-Link's bridge takes seconds to turn a
# line around while the banner is also going out (see smoke.handshake).
BANNER_SECONDS = 20.0
RUN_SECONDS = 20.0
RUN_ATTEMPTS = 2
# Enough past _ebss to be clear of it, but still well below the stack.
PAST_EBSS_WORDS = 4
# A runaway symbol table should not turn into a command line megabytes long.
MAX_FILL_WORDS = 4096


def symbols(gcc: str, elf: pathlib.Path) -> dict:
    """The linker-script symbols this test needs, from the ELF itself."""
    out = subprocess.run([f"{gcc}/riscv-none-elf-nm", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)
        if m and m.group(2) in ("_data_vma", "_edata", "_sbss", "_ebss"):
            found[m.group(2)] = int(m.group(1), 16)
    missing = {"_data_vma", "_ebss"} - set(found)
    if missing:
        raise smoke.Failure(f"{elf.name} defines no {sorted(missing)}; "
                            f"cores/arduino/sections.ld should PROVIDE them")
    return found


def probe_rs(bench: smoke.Bench, *args) -> str:
    cmd = [str(pathlib.Path(bench.probe_rs) / "probe-rs"), *args,
           "--chip", chip_of(bench)]
    if bench.select_probe:
        cmd += ["--probe", f"1a86:8010:{bench.probe}"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode:
        raise smoke.Failure("probe-rs " + args[0] + " failed\n"
                            + proc.stdout + proc.stderr)
    return proc.stdout


def chip_of(bench: smoke.Bench) -> str:
    """What boards.txt tells the upload recipe to pass probe-rs."""
    key = (f"{bench.board}.menu.pnum.{bench.pnum}.build.probe_rs_chip="
           if bench.pnum != "ANY" else f"{bench.board}.menu.pnum.ANY.build.probe_rs_chip=")
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(key):
            return line.split("=", 1)[1].strip()
    raise smoke.Failure(f"boards.txt has no probe_rs_chip for {bench.board}:"
                        f"{bench.pnum}")


def run(bench: smoke.Bench, log=print) -> dict:
    """Flash, fill RAM, reset, and return the markers the sketch reported."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        # stage_sketch rather than a copy of the .ino: the sketch now includes
        # testcmd.h, and arduino-cli compiles the sketch folder and nothing
        # above it.
        sketch_dir = stage_sketch(HERE / "sketch", tmp / "crt0_probe")
        env = smoke.sketchbook(tmp)
        built = smoke.build(bench, sketch_dir, tmp, env, log=log)
        smoke.upload(bench, built, sketch_dir, env, log=log)

        elf = built / "crt0_probe.ino.elf"
        sym = symbols(bench.gcc, elf)
        first, last = sym["_data_vma"], sym["_ebss"] + 4 * PAST_EBSS_WORDS
        words = (last - first) // 4
        if words > MAX_FILL_WORDS:
            raise smoke.Failure(f"{words} words is more than this test will "
                                f"write in one probe-rs call")
        log(f"   filling {words} words from 0x{first:08x} with "
            f"0x{PATTERN:08X} (_ebss is 0x{sym['_ebss']:08x})")
        probe_rs(bench, "write", "b32", hex(first),
                 *([f"0x{PATTERN:X}"] * words))

        import serial
        with serial.Serial(bench.port, bench.baud, timeout=0.3) as uart:
            uart.reset_input_buffer()
            probe_rs(bench, "reset")
            link = smoke.Link(uart)
            # Wait for the banner rather than reading for a fixed window: the
            # sketch repeats it every half second, so however long the reset
            # takes, it is caught. A window was the old way and it had to be
            # long enough for the worst case on every board.
            if not link.wait("crt0_probe READY", BANNER_SECONDS):
                raise smoke.Failure(
                    f"no 'crt0_probe READY' within {BANNER_SECONDS:g}s after "
                    f"reset: the sketch is not running, or Serial is miswired")
            # PING before RUN, the way smoke.py does. It proves the host's
            # bytes are reaching the target before anything depends on them,
            # so "RUN went missing" and "the board cannot read at all" stop
            # looking the same - and it absorbs the first, slowest round trip.
            if not smoke.handshake(link):
                raise smoke.Failure(
                    "the banner arrives but PING is not answered: the probe's "
                    "TX is probably not wired to the board's RX")
            for attempt in range(RUN_ATTEMPTS):
                link.send("RUN\n")
                if link.wait("crt0_probe done failures=", RUN_SECONDS):
                    break
                if attempt + 1 == RUN_ATTEMPTS:
                    raise smoke.Failure(
                        f"the report did not finish within {RUN_SECONDS:g}s of "
                        f"RUN, {RUN_ATTEMPTS} times over")
                log("   no report; sending RUN again")
            link.wait("\n", 1.0)          # let the count itself arrive
            got = link.buf

    text = got.decode(errors="replace")
    log("--- output " + "-" * 48)
    log(text.strip() or "(nothing received)")
    log("-" * 59)
    markers = {m.group(1): int(m.group(2), 16)
               for m in re.finditer(r"^(\w+)=([0-9A-F]{8})\s*$", text, re.M)}
    return {"markers": markers, "output": text, "symbols": sym,
            "board": bench.board}


@pytest.fixture(scope="module")
def crt0(bench):
    try:
        return run(bench)
    except smoke.Failure as e:
        pytest.fail(str(e))


def test_setup_was_reached(crt0):
    """Nothing else below means anything if the sketch never ran."""
    assert "crt0_probe READY" in crt0["output"], (
        "no banner: crt0 did not reach setup(), or Serial is miswired")
    assert "crt0_probe done failures=0" in crt0["output"], (
        "the sketch reported a failing check of its own; the lines above say "
        "which")


def test_the_fill_really_happened(crt0):
    """The control. Without it, a zeroed .bss could just be RAM powering up at 0.

    Checked here rather than on the board: the host chose the pattern, so the
    host is the side that can compare against it without the constant existing
    in two places and drifting.
    """
    assert crt0["markers"].get("past_ebss") == PATTERN, (
        f"the word past _ebss reads "
        f"{crt0['markers'].get('past_ebss'):#010x} rather than {PATTERN:#010x}, "
        f"so the pattern never reached the part and the two checks below prove "
        f"nothing")


def test_bss_was_zeroed(crt0):
    """crt0 cleared .bss over the pattern, before the first constructor ran."""
    assert "bss_zeroed PASS" in crt0["output"], crt0["markers"]


def test_data_was_copied_from_flash(crt0):
    """And copied .data in, rather than leaving whatever RAM held."""
    assert "data_copied_from_flash PASS" in crt0["output"], crt0["markers"]


def test_init_array_ran(crt0):
    """C++ global constructors, which is what .init_array is there for."""
    assert "init_array_ran PASS" in crt0["output"], crt0["markers"]
