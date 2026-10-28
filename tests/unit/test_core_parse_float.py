"""Stream::parseFloat's arithmetic (cores/arduino/ch32rv_parse_float.h), on the host.

format/parse_float_cases.c is built with the host compiler once per CH32RV_PARSE_FLOAT and under both kinds
of constants (-fsingle-precision-constant is what the Float entries add) and compares against strtof: double
must give upstream's float, integers the nearest float to 11 significant digits, float at most the last bit off.
"""
import pathlib
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
CORE = REPO / "cores/arduino"
CASES = pathlib.Path(__file__).with_name("format") / "parse_float_cases.c"


@pytest.mark.parametrize("parse", [1, 2, 3])
@pytest.mark.parametrize("fp_float", [0, 1])
def test_parse_float(tmp_path, parse, fp_float):
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no host C compiler")
    exe = tmp_path / "parse_float_cases"
    flags = [f"-DCH32RV_PARSE_FLOAT={parse}", f"-DCH32RV_FP_FLOAT={fp_float}"]
    if fp_float:
        flags.append("-fsingle-precision-constant")
    subprocess.run([cc, "-std=c11", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", f"-I{CORE}",
                    *flags, str(CASES), "-o", str(exe), "-lm"], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout
    assert "failures 0" in run.stdout
