"""Floating Point (rtlib) and parseFloat (parsefloat): two menus, every pair selectable.

parseFloat's first entry defines nothing, so ch32rv_parse_float.h parses the way the arithmetic does (integers
under Float, double under Double): the default pair, Float + printf %f with parseFloat in integers, and each newlib
entry keep the parsing they had as one menu of 17.
"""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
RTLIB = ["fltf", "flt", "dbl", "dblf", "nanofp", "nanofs", "dblfs", "fltfs", "nanofps"]
PARSE = {"auto": "", "int": "-DCH32RV_PARSE_FLOAT=2", "float": "-DCH32RV_PARSE_FLOAT=3",
         "double": "-DCH32RV_PARSE_FLOAT=1"}


def test_every_board_has_both_menus_default_first():
    rtlib, parse, defines = {}, {}, {}
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        if m := re.fullmatch(r"(\w+)\.menu\.rtlib\.(\w+)=.*", line):
            rtlib.setdefault(m.group(1), []).append(m.group(2))
        if m := re.fullmatch(r"(\w+)\.menu\.parsefloat\.(\w+)=.*", line):
            parse.setdefault(m.group(1), []).append(m.group(2))
        if m := re.fullmatch(r"(\w+)\.menu\.parsefloat\.(\w+)\.build\.parse_defines=(.*)", line):
            defines[(m.group(1), m.group(2))] = m.group(3)
    assert rtlib and set(rtlib) == set(parse)
    for board in rtlib:
        assert rtlib[board] == RTLIB, board
        assert parse[board] == list(PARSE), board
        for key, want in PARSE.items():
            assert defines[(board, key)] == want, (board, key)


def test_the_parse_define_reaches_c_and_cpp_and_defaults_to_nothing():
    platform = (REPO / "platform.txt").read_text(encoding="utf-8")
    assert re.search(r"^build\.parse_defines=$", platform, re.M)
    for flags in ("compiler.c.flags", "compiler.cpp.flags"):
        assert "{build.parse_defines}" in re.search(rf"^{re.escape(flags)}=.*$", platform, re.M).group(0), flags
    header = (REPO / "cores/arduino/internal/ch32rv_parse_float.h").read_text(encoding="utf-8")
    assert "#define CH32RV_PARSE_FLOAT (CH32RV_FP_FLOAT ? 2 : 1)" in header
