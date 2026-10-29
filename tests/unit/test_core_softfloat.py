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
