"""The product-board FQBN accepts the platform-wide pnum option in CI."""

import pathlib

from loader import load

matrix = load("tests/compile/compile_matrix.py", "uiapduino_compile_matrix")
stage = load("tests/sketches/stage.py", "uiapduino_compile_stage")


def test_analog_read_compiles_with_generic_pnum(repo, gcc_bin, arduino_cli, workdir):
    work = workdir / "uiapduino-analog-read"
    env = matrix.sandbox(work)
    matrix.link_platform(work)
    source = repo / "libraries" / "CH32" / "examples" / "AnalogRead"
    staged = stage.stage_sketch(source, work / "AnalogRead")
    rc, output = matrix.compile_one(
        env,
        "ch32-riscv-ug:ch32v:UIAPDUINO_V003_V14:pnum=ANY",
        gcc_bin,
        work / "build",
        staged,
    )
    assert rc == 0, output
