"""The core's printf formatting and fixed-point text (cores/arduino/ch32rv_format.c), on the host.

format/format_cases.c is built with the host compiler once per Floating Point menu configuration (float or
double arithmetic, printf's %f on or off) and compares the core against the host's snprintf where both must
agree, and against fixed strings where the core differs on purpose (Print::printFloat's rounding of exact
halves, "ovf", what it does not do printed as written).
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
CORE = REPO / "cores/arduino"
CASES = pathlib.Path(__file__).with_name("format") / "format_cases.c"


@pytest.mark.parametrize("fp_float", [0, 1])
@pytest.mark.parametrize("printf_float", [0, 1])
def test_format(tmp_path, fp_float, printf_float):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    exe = tmp_path / "format_cases"
    flags = [f"-DCH32RV_FP_FLOAT={fp_float}", f"-DCH32RV_PRINTF_FLOAT={printf_float}"]
    if fp_float:
        flags.append("-fsingle-precision-constant")
    subprocess.run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-format", f"-I{CORE}", *flags,
                    str(CORE / "ch32rv_format.c"), str(CASES), "-o", str(exe)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    assert "failures 0" in run.stdout
