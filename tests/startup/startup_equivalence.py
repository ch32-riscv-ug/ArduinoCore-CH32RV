#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""The unified crt0 leaves the target in the same state as each EVT startup.

W-2. The core owns its startup, vector table and linker script (ADR-0003)
rather than carrying WCH's per-family files. That is only safe if it lands the
machine in the same state, so for every family this builds the same main.c
twice - once against WCH's startup, once against ours - and compares the vector
table and the CSR writes.

  CH32_MIRROR_ROOT=<dir with the CH32* clones> \\
      uv run tests/startup/startup_equivalence.py <workdir>

Normally reached through `pytest` (tests/startup/test_startup_equivalence.py).

The EVT mirrors are large clones of other repositories, so they are not fetched
into .tools; point CH32_MIRROR_ROOT at wherever they are.

CH32H417 is excluded: it boots through loadcode rather than a reset vector, so
there is nothing equivalent to compare. See README.ja.md.
"""
import argparse
import dataclasses
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tests" / "compile"))

from compile_matrix import Failure, gcc_bin, tool      # noqa: E402

CRT0 = REPO / "cores" / "arduino" / "crt0_ch32.S"

MAIN_C = """\
#include <stdint.h>
void SystemInit(void) {}
volatile uint32_t counter;
void SysTick_Handler(void) { counter = 1; }
int main(void) { for (;;) counter++; }
"""


@dataclasses.dataclass(frozen=True)
class Family:
    tag: str
    march: str
    mabi: str
    startup: str          # relative to the mirror root
    ld: str               # relative to the mirror root
    defines: tuple        # what the unified crt0 needs to match this family


F = Family
FAMILIES = (
    F("v003", "rv32ec_zicsr", "ilp32e",
      "CH32V003/EVT/EXAM/SRC/Startup/startup_ch32v00x.S",
      "CH32V003/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x1880", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_HIGHCODE")),
    F("v00x", "rv32emc_zicsr", "ilp32e",
      "CH32V006/EVT/EXAM/SRC/Startup/startup_ch32v00X.S",
      "CH32V006/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x1880", "-DCH32_INTSYSCR_INIT=0x3")),
    F("v20x_d6", "rv32imac_zicsr", "ilp32",
      "CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D6.S",
      "CH32V20x/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_CORECFGR=0x1f")),
    F("v20x_d8", "rv32imac_zicsr", "ilp32",
      "CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D8.S",
      "CH32V20x/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_CORECFGR=0x1f")),
    F("v20x_d8w", "rv32imac_zicsr", "ilp32",
      "CH32V20x/EVT/EXAM/SRC/Startup/startup_ch32v20x_D8W.S",
      "CH32V20x/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_CORECFGR=0x1f")),
    F("v205", "rv32imc_zicsr", "ilp32",
      "CH32V205/EVT/EXAM/SRC/Startup/startup_ch32v205.S",
      "CH32V205/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x7", "-DCH32_CORECFGR=0x21",
       "-DCH32_CSR_BC1=0x1")),
    F("m030", "rv32imc_zicsr", "ilp32",
      "CH32M030/EVT/EXAM/SRC/Startup/startup_ch32m030.S",
      "CH32M030/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_CORECFGR=0x21",
       "-DCH32_CSR_BC1=0x1")),
    F("v307_d8", "rv32imafc_zicsr", "ilp32f",
      "CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8.S",
      "CH32V307/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x6088", "-DCH32_INTSYSCR_INIT=0x0b", "-DCH32_CORECFGR=0x1f")),
    F("v307_d8c", "rv32imafc_zicsr", "ilp32f",
      "CH32V307/EVT/EXAM/SRC/Startup/startup_ch32v30x_D8C.S",
      "CH32V307/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x6088", "-DCH32_INTSYSCR_INIT=0x0b", "-DCH32_CORECFGR=0x1f")),
    F("v4x7", "rv32imac_zicsr", "ilp32",
      "CH32V407/EVT/EXAM/SRC/Startup/startup_ch32v4x7.S",
      "CH32V407/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x688", "-DCH32_INTSYSCR_INIT=0x07", "-DCH32_CORECFGR=0x21",
       "-DCH32_CSR_BC1=0x01", "-DCH32_CSR805_CLR=0x100")),
    F("x035", "rv32imac_zicsr", "ilp32",
      "CH32X035/EVT/EXAM/SRC/Startup/startup_ch32x035.S",
      "CH32X035/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_CORECFGR=0x1f")),
    F("x3x5", "rv32imafc_zicsr", "ilp32f",
      "CH32X315/EVT/EXAM/SRC/Startup/startup_ch32x3x5.S",
      "CH32X315/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x6088", "-DCH32_INTSYSCR_INIT=0x07",
       "-DCH32_CORECFGR=0x123703E1", "-DCH32_CSR_BC1=0x01")),
    F("v103", "rv32imac_zicsr", "ilp32",
      "CH32V103/EVT/EXAM/SRC/Startup/startup_ch32v10x.S",
      "CH32V103/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_MTVEC_MODE=1")),
    F("l103", "rv32imac_zicsr", "ilp32",
      "CH32L103/EVT/EXAM/SRC/Startup/startup_ch32l103.S",
      "CH32L103/EVT/EXAM/SRC/Ld/Link.ld",
      ("-DCH32_MSTATUS_INIT=0x88", "-DCH32_INTSYSCR_INIT=0x3", "-DCH32_CORECFGR=0x1f")),
)


# Where the mirrors usually sit on a bench that has them. Searched only as a
# fallback, so a bench with them in the usual place needs no environment at all
# and CH32_MIRROR_ROOT still wins when it is set.
LIKELY = (pathlib.Path.home() / "dev_wch", pathlib.Path.home() / "mirrors")


def find_mirror_root():
    """The EVT mirror root as a string, or None if this machine has none.

    Both startup tests ask the same question - one to build, one to re-check
    interrupts.csv - so it is answered here rather than in either of them.
    """
    root = os.environ.get("CH32_MIRROR_ROOT")
    if root:
        return root
    for d in LIKELY:
        if (d / "CH32V003" / "EVT").is_dir():
            return str(d)
    return None


def mirror_root() -> pathlib.Path:
    root = find_mirror_root()
    if not root:
        raise Failure("set CH32_MIRROR_ROOT to the directory holding the "
                      "CH32* EVT mirrors")
    return pathlib.Path(root)


def python(*args, cwd=None) -> subprocess.CompletedProcess:
    return subprocess.run(["uv", "run", "--no-project", "python", *args],
                          cwd=cwd, capture_output=True, text=True)


def unified_ld(evt_ld: pathlib.Path, dest: pathlib.Path) -> None:
    """Give the EVT linker script a .vector output section if it lacks one.

    The unified crt0 always emits .vector; several EVT scripts predate that and
    would silently drop it.
    """
    src = evt_ld.read_text(encoding="utf-8")
    if ".vector" not in src:
        src = src.replace(
            "} >FLASH AT>FLASH",
            "} >FLASH AT>FLASH\n\t.vector : { *(.vector); . = ALIGN(64); } "
            ">FLASH AT>FLASH", 1)
    dest.write_text(src, encoding="utf-8")


def build_family(gcc, work, root, fam) -> list:
    """Build both ELFs for one family and compare them. Returns the report."""
    startup = root / fam.startup
    if not startup.exists():
        raise Failure(f"{fam.tag}: no EVT startup at {startup}")
    gcc_exe = tool(gcc, "gcc")
    arch = [f"-march={fam.march}", f"-mabi={fam.mabi}"]

    def run(cmd, what):
        proc = subprocess.run(cmd, cwd=work, capture_output=True, text=True)
        if proc.returncode != 0:
            raise Failure(f"{fam.tag}: {what} failed\n{proc.stderr[-1500:]}")

    # The vector spec is generated from the local mirror and must not be
    # committed - it is derived from someone else's source tree.
    proc = python(str(HERE / "extract_vectors.py"), str(startup),
                  f"vectors_{fam.tag}.inc", cwd=work)
    if proc.returncode != 0:
        raise Failure(f"{fam.tag}: extract_vectors failed\n{proc.stderr}")

    # Where the EVT build puts its table: .vector when the startup names
    # _vector_base, otherwise it lands in .init.
    evt_section = (".vector" if "_vector_base" in
                   startup.read_text(encoding="utf-8", errors="replace")
                   else ".init")

    evt_ld = work / f"ld_{fam.tag}.ld"
    evt_ld.write_text((root / fam.ld).read_text(encoding="utf-8"),
                      encoding="utf-8")
    unified_ld(evt_ld, work / f"ld_{fam.tag}_uni.ld")

    run([gcc_exe, *arch, "-Os", "-c", "main.c", "-o", f"main_{fam.tag}.o"],
        "compiling main.c")
    run([gcc_exe, *arch, "-Os", "-c", str(startup),
         "-o", f"startup_evt_{fam.tag}.o"], "assembling the EVT startup")
    run([gcc_exe, *arch, "-nostartfiles", "-T", f"ld_{fam.tag}.ld",
         "-Wl,--gc-sections", f"main_{fam.tag}.o", f"startup_evt_{fam.tag}.o",
         "-o", f"evt_{fam.tag}.elf"], "linking the EVT build")
    run([gcc_exe, *arch, "-Os", "-I.", f"-DCH32_VECTORS=vectors_{fam.tag}.inc",
         *fam.defines, "-c", str(CRT0), "-o", f"startup_uni_{fam.tag}.o"],
        "assembling the unified crt0")
    run([gcc_exe, *arch, "-nostartfiles", "-T", f"ld_{fam.tag}_uni.ld",
         "-Wl,--gc-sections", f"main_{fam.tag}.o", f"startup_uni_{fam.tag}.o",
         "-o", f"uni_{fam.tag}.elf"], "linking the unified build")

    proc = python(str(HERE / "compare.py"), str(gcc),
                  f"vectors_{fam.tag}.inc", f"evt_{fam.tag}.elf",
                  f"uni_{fam.tag}.elf", evt_section, cwd=work)
    print(proc.stdout, end="", flush=True)
    if proc.returncode != 0:
        print(proc.stderr, end="", file=sys.stderr, flush=True)
    return proc.returncode == 0


def run(work: pathlib.Path) -> dict:
    work.mkdir(parents=True, exist_ok=True)
    gcc = gcc_bin()
    root = mirror_root()
    (work / "main.c").write_text(MAIN_C, encoding="utf-8")

    results = {}
    for fam in FAMILIES:
        print(f"===== {fam.tag} ({fam.march}/{fam.mabi})", flush=True)
        results[fam.tag] = build_family(gcc, work, root, fam)

    failed = [t for t, ok in results.items() if not ok]
    if failed:
        raise Failure(f"{len(failed)} of {len(results)} families differ: "
                      + ", ".join(failed))
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
