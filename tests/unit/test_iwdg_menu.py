"""The Watchdog menu (boards.txt): USER bit 0 IWDG_SW on the series whose option byte writer has run on the chip.

The menu opens on "keep", which defines nothing (the chip as it is set); "sw" (the factory value, started by
wdtEnable()) and "hw" (running from power-on) both have ch32rv_option_user.c set the bit at boot
(docs/adr/0017-option-byte-menus.ja.md).
"""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
SERIES = {"CH32V002", "CH32V003", "CH32V004", "CH32V005", "CH32V006", "CH32V007", "CH32M007",
          "CH32X033", "CH32X035", "CH32X305", "CH32X315", "CH32V303", "CH32V305", "CH32V307", "CH32V317",
          "CH32V203", "CH32V208", "CH32L103", "CH32M103", "CH32V205", "CH32V407", "CH32V467"}


def test_the_series_open_on_keep_and_set_bit_0_either_way():
    entries, values = {}, {}
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        if m := re.fullmatch(r"(\w+)\.menu\.iwdg\.(\w+)=(.*)", line):
            entries.setdefault(m.group(1), []).append(m.group(2))
        if m := re.fullmatch(r"(\w+)\.menu\.iwdg\.(\w+)\.build\.iwdg_defines=(.*)", line):
            values[(m.group(1), m.group(2))] = m.group(3)
    assert set(entries) == SERIES
    for series in SERIES:
        assert entries[series] == ["keep", "sw", "hw"], series
        assert values[(series, "keep")] == "", series
        assert values[(series, "sw")] == "-DCH32RV_IWDG_USER=0x01u -DCH32RV_IWDG_MASK=0x01u", series
        assert values[(series, "hw")] == "-DCH32RV_IWDG_USER=0x00u -DCH32RV_IWDG_MASK=0x01u", series


def test_the_check_runs_from_crt0_only_with_the_define():
    platform = (REPO / "platform.txt").read_text(encoding="utf-8")
    assert re.search(r"^build\.iwdg_defines=$", platform, re.M)
    for flags in ("compiler.c.flags", "compiler.cpp.flags", "compiler.S.flags"):
        assert "{build.iwdg_defines}" in re.search(rf"^{re.escape(flags)}=.*$", platform, re.M).group(0), flags
    crt0 = (REPO / "cores/arduino/crt0_ch32.S").read_text(encoding="utf-8")
    assert "#ifdef CH32RV_IWDG_USER" in crt0 and "call ch32rv_iwdg_check" in crt0
