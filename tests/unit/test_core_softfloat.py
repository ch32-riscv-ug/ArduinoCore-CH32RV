"""The core's soft-float pieces (cores/arduino/ch32rv_softfloat.c), on the host: __clzsi2 against the host's
clz, ch32rv_ctz32 / ch32rv_popcount32 (internal/ch32rv_bits.h, without a multiply) against ctz / popcount, __extendsfdf2 and __truncdfsf2 against the host's float <-> double conversions, over
every 251st 32-bit value and the edges (all 2^32 of clz and extend were checked once, 2026-10-29); for
__truncdfsf2 each value is a double's high word, with low words around the rounding point. The subtractions only call the addition with b's
sign turned over (compiler-rt's now; the core's were checked against libgcc's on V003 and X035).
"""
import json
import os
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]


def toolchain_bin() -> pathlib.Path | None:
    """The toolchain's bin/: CH32_TOOLCHAIN_BIN, else the pinned one under <repo>/.tools
    (tools/index/fetch_tools.py --tool ch32rv-toolchain)."""
    override = os.environ.get("CH32_TOOLCHAIN_BIN")
    if override:
        path = pathlib.Path(override)
    else:
        frag = json.loads((REPO / "tools/index/tools_ch32rv_toolchain.json").read_text(encoding="utf-8"))
        path = REPO / ".tools" / frag["name"] / frag["version"] / "bin"
    return path if any((path / n).exists() for n in ("clang", "clang.exe")) else None


@pytest.fixture(scope="module")
def clang_bin() -> pathlib.Path:
    path = toolchain_bin()
    if path is None:
        pytest.skip("the toolchain is not fetched: uv run tools/index/fetch_tools.py --tool ch32rv-toolchain")
    return path


SRC = REPO / "cores/arduino/ch32rv_softfloat.c"
CASES = pathlib.Path(__file__).with_name("format") / "softfloat_cases.c"


def test_clz_and_extend(tmp_path):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    exe = tmp_path / "softfloat_cases"
    # __riscv_flen=32: the double half only, as on V30x.
    subprocess.run([cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-D__riscv_flen=32",
                    "-D__clzsi2=core_clz", "-D__extendsfdf2=core_ext", "-DCH32RV_BITS_PORTABLE", f"-I{REPO / 'cores/arduino'}", "-D__truncdfsf2=core_trunc",
                    str(SRC), str(CASES), "-o", str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    assert "failures 0" in run.stdout


def test_a_call_made_after_lto_finds_the_core_definition(clang_bin, tmp_path):
    """core.a holds the object as LTO bitcode. When a non-LTO object (libc's strtod, here sub.o) brings it in with
    __subdf3, the float to double conversion the code generator makes after LTO (__extendsfdf2) still needs a
    definition: lld extracts the bitcode members that define runtime library calls before LTO, and `used` keeps
    them (2026-10-31, under GCC: Serial.println(Serial.parseFloat(), 6) did not link on V003 and X035)."""
    clang = str(clang_bin / "clang")
    flags = ["--target=riscv32-unknown-elf", "-march=rv32ec_zicsr_xwchc", "-mabi=ilp32e", "-Oz",
             "-ffunction-sections", "-fdata-sections"]
    (tmp_path / "sub.c").write_text("double sub(double a, double b) { return a - b; }\n")
    (tmp_path / "main.c").write_text("double sub(double a, double b);\nvolatile float f = 1.5f;\nvolatile double g;\n"
                                     "void _start(void) { g = f; g = sub(g, 1.0); for (;;) {} }\n")
    subprocess.run([clang, *flags, "-c", "sub.c", "-o", "sub.o"], cwd=tmp_path, check=True)
    subprocess.run([clang, *flags, "-flto", "-c", str(SRC), "-o", "softfloat.o"], cwd=tmp_path, check=True)
    subprocess.run([str(clang_bin / "llvm-ar"), "rcs", "libcore.a", "softfloat.o"], cwd=tmp_path, check=True)
    link = subprocess.run([clang, *flags, "-flto", "-fuse-ld=lld", "-nostdlib", "main.c", "sub.o",
                           "-Wl,--gc-sections", "-Wl,--start-group", "libcore.a", "-lclang_rt.builtins",
                           "-Wl,--end-group", "-o", "a.elf"],
                          cwd=tmp_path, capture_output=True, text=True)
    assert link.returncode == 0, link.stderr
    syms = subprocess.run([str(clang_bin / "llvm-nm"), "a.elf"], cwd=tmp_path, capture_output=True,
                          text=True, check=True).stdout
    assert " T __extendsfdf2" in syms
