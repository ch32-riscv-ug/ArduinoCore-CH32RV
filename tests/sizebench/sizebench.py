#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""How much flash and RAM each libc variant costs on this core.

R-09. Sizes only - nothing is executed. This is the measurement ADR-0004's
choice of newlib-nano rests on, and it lives in the suite so the numbers stay
reproducible rather than being quoted from a one-off run.

  uv run tests/sizebench/sizebench.py <workdir>

Normally reached through `pytest` (tests/test_sizebench.py).

Built against the real crt0 and sections.ld the core ships, not copies, so the
numbers describe this platform rather than an approximation of it.
"""
import argparse
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tests" / "compile"))

from compile_matrix import Failure, gcc_bin, tool     # noqa: E402

CASES = sorted(list((HERE / "cases").glob("*.c")) + list((HERE / "cases").glob("*.cpp")))

# (tag, -march, -mabi)
ARCHES = [("rv32ec", "rv32emc_zicsr", "ilp32e"),
          ("rv32imac", "rv32imac_zicsr", "ilp32")]

COMMON = ["-Os", "-g", "-ffunction-sections", "-fdata-sections"]
CXXFLAGS = ["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
CRT_DEFS = ["-DCH32_VECTORS=vectors_v00x.inc", "-DCH32_MSTATUS_INIT=0x1880",
            "-DCH32_INTSYSCR_INIT=0x3"]

# Measurement-only memory map, deliberately generous so no case is clipped by a
# real part's flash limit. Whether a case fits a given SKU is judged from the
# numbers, not enforced here.
LDSCRIPT = """\
ENTRY( _start )
MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1M
    RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
}
INCLUDE sections.ld
"""


def build_one(gcc, work, arch_tag, march, mabi, case, flavor, extra_link):
    """Compile and link one case; return (text, data, bss)."""
    cc = tool(gcc, "g++") if case.suffix == ".cpp" else tool(gcc, "gcc")
    std = CXXFLAGS if case.suffix == ".cpp" else []
    tag = f"{case.stem}_{flavor}_{arch_tag}"
    arch = [f"-march={march}", f"-mabi={mabi}"]
    core = REPO / "cores" / "arduino"

    def run(cmd):
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            raise Failure(f"{tag}: {' '.join(cmd[:1])} failed\n{proc.stderr[-1500:]}")

    run([tool(gcc, "gcc"), *arch, *COMMON, f"-I{core}", *CRT_DEFS,
         "-c", str(core / "crt0_ch32.S"), "-o", str(work / f"crt0_{tag}.o")])
    run([tool(gcc, "gcc"), *arch, *COMMON,
         "-c", str(HERE / "syscalls.c"), "-o", str(work / f"sys_{tag}.o")])
    run([cc, *arch, *COMMON, *std, "-c", str(case),
         "-o", str(work / f"case_{tag}.o")])
    run([cc, *arch, "-nostartfiles", "-Wl,--gc-sections",
         "-Wl,--no-warn-rwx-segments", f"-L{core}",
         f"-T{work / 'sizebench.ld'}", *extra_link,
         str(work / f"case_{tag}.o"), str(work / f"sys_{tag}.o"),
         str(work / f"crt0_{tag}.o"), "-o", str(work / f"{tag}.elf")])

    out = subprocess.run([tool(gcc, "size"), str(work / f"{tag}.elf")],
                         capture_output=True, text=True, check=True).stdout
    text, data, bss = (int(x) for x in out.splitlines()[1].split()[:3])
    return text, data, bss


def run(work: pathlib.Path) -> dict:
    work.mkdir(parents=True, exist_ok=True)
    gcc = gcc_bin()
    (work / "sizebench.ld").write_text(LDSCRIPT, encoding="utf-8")
    if not CASES:
        raise Failure(f"no cases under {HERE / 'cases'}")

    rows, results = [], {}
    for arch_tag, march, mabi in ARCHES:
        flavours = [("nano", ["--specs=nano.specs"]), ("full", [])]
        for case in CASES:
            for flavor, extra in flavours:
                sizes = build_one(gcc, work, arch_tag, march, mabi, case,
                                  flavor, extra)
                results[(case.stem, flavor, arch_tag)] = sizes
                rows.append((case.stem, flavor, arch_tag, *sizes))
        # nano plus float printf: the case where nano needs an explicit opt-in,
        # which is what the printf menu in boards.txt turns on.
        case = HERE / "cases" / "12_printf_float.c"
        if case.exists():
            sizes = build_one(gcc, work, arch_tag, march, mabi, case, "nano+f",
                              ["--specs=nano.specs", "-Wl,-u,_printf_float"])
            results[(case.stem, "nano+f", arch_tag)] = sizes
            rows.append((case.stem, "nano+f", arch_tag, *sizes))

    table = ["| case | libc | arch | text | data | bss |",
             "|---|---|---|---:|---:|---:|"]
    table += [f"| {c} | {f} | {a} | {t} | {d} | {b} |" for c, f, a, t, d, b in rows]
    (work / "results.md").write_text("\n".join(table) + "\n", encoding="utf-8")
    print("\n".join(table), flush=True)
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
