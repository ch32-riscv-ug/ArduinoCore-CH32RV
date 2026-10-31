"""The core's soft-float pieces (cores/arduino/ch32rv_softfloat.c), on the host: __clzsi2 against the host's
clz and __extendsfdf2 against the host's float to double conversion, over every 251st 32-bit value and the
edges (all 2^32 of each were checked once, 2026-10-29). The subtractions only call the addition with b's
sign turned over; they were checked against libgcc's on V003 and X035.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "cores/arduino/ch32rv_softfloat.c"
CASES = pathlib.Path(__file__).with_name("format") / "softfloat_cases.c"


def test_clz_and_extend(tmp_path):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    exe = tmp_path / "softfloat_cases"
    # __riscv_flen=32: the double half only, as on V30x; the float subtraction needs a soft __addsf3.
    subprocess.run([cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-D__riscv_flen=32",
                    "-D__clzsi2=core_clz", "-D__extendsfdf2=core_ext", "-D__adddf3=core_add", "-D__subdf3=core_sub",
                    str(SRC), str(CASES), "-o", str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    assert "failures 0" in run.stdout


def test_a_call_gcc_makes_after_lto_finds_the_core_definition(tmp_path):
    """core.a holds the object as LTO IR. When a non-LTO object (libc's strtod, here sub.o) brings it in with
    __subdf3, the plugin owns every definition in it; without `used`, __extendsfdf2, which nothing calls before
    LTO, is dropped, and the float to double conversion GCC makes after LTO has no definition (2026-10-31:
    Serial.println(Serial.parseFloat(), 6) did not link on V003 and X035)."""
    found = sorted(d for d in (REPO / ".tools" / "xpack-riscv-none-elf-gcc").glob("*") if (d / "bin").is_dir())
    if not found:
        pytest.skip("the RISC-V toolchain is not fetched: uv run tools/index/fetch_tools.py")
    gcc = str(found[-1] / "bin" / "riscv-none-elf-gcc")
    flags = ["-march=rv32ec_zicsr", "-mabi=ilp32e", "-Os", "-ffunction-sections", "-fdata-sections"]
    (tmp_path / "sub.c").write_text("double sub(double a, double b) { return a - b; }\n")
    (tmp_path / "main.c").write_text("double sub(double a, double b);\nvolatile float f = 1.5f;\nvolatile double g;\n"
                                     "void _start(void) { g = f; g = sub(g, 1.0); for (;;) {} }\n")
    subprocess.run([gcc, *flags, "-c", "sub.c", "-o", "sub.o"], cwd=tmp_path, check=True)
    subprocess.run([gcc, *flags, "-flto", "-ffat-lto-objects", "-c", str(SRC), "-o", "softfloat.o"],
                   cwd=tmp_path, check=True)
    subprocess.run(["ar", "rcs", "libcore.a", "softfloat.o"], cwd=tmp_path, check=True)
    link = subprocess.run([gcc, *flags, "-flto", "-nostartfiles", "-nostdlib", "main.c", "sub.o", "-Wl,--gc-sections",
                           "-Wl,--start-group", "libcore.a", "-Wl,--end-group", "-lgcc", "-o", "a.elf"],
                          cwd=tmp_path, capture_output=True, text=True)
    assert link.returncode == 0, link.stderr
