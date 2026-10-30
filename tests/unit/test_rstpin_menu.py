"""The Reset Pin menu (boards.txt) against the reference manuals' USER fields.

Each series whose reset input is an option byte setting gets the menu: USER
[4:3] RST_MODE (10b the reset pin with 12 ms ignored, 11b a GPIO) on V00x,
M007, X033/X035 and M030; X305/X315's bit 3 CFGRSTEN (0 the reset pin). The
factory entry comes first and defines nothing - the chip is left as it is set,
at no cost - and the other has ch32rv_option_user.c set the chip at boot.
"""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]

# series -> (factory entry, USER value of the other entry, mask)
EXPECTED = {
    **{s: ("reset", 0x18, 0x18) for s in ("CH32V002", "CH32V003", "CH32V004", "CH32V005", "CH32V006")},
    **{s: ("gpio", 0x10, 0x18) for s in ("CH32V007", "CH32M007", "CH32X033", "CH32X035", "CH32M030")},
    **{s: ("gpio", 0x00, 0x08) for s in ("CH32X305", "CH32X315")},
}


def boards() -> list[tuple[str, str]]:
    out = []
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep and not line.startswith("#"):
            out.append((key, value))
    return out


def test_each_series_offers_its_factory_setting_first_and_builds_nothing_in_for_it():
    entries = {}
    for key, value in boards():
        m = re.fullmatch(r"(\w+)\.menu\.rstpin\.(\w+)", key)
        if m:
            entries.setdefault(m.group(1), []).append(m.group(2))
    assert set(entries) == set(EXPECTED)
    b = dict(boards())
    for series, (factory, value, mask) in EXPECTED.items():
        other = "gpio" if factory == "reset" else "reset"
        assert entries[series] == [factory, other], series
        assert "factory setting" in b[f"{series}.menu.rstpin.{factory}"], series
        assert b[f"{series}.menu.rstpin.{factory}.build.rstpin_defines"] == "", series
        assert (b[f"{series}.menu.rstpin.{other}.build.rstpin_defines"]
                == f"-DCH32RV_RSTPIN_USER={value:#04x}u -DCH32RV_RSTPIN_MASK={mask:#04x}u"), series


def test_a_series_with_a_dedicated_reset_pin_and_the_product_board_have_no_menu():
    keys = [k for k, _ in boards()]
    for board in ("CH32V103", "CH32V203", "CH32V307", "CH32L103", "CH32H417", "UIAPDUINO_V003_V14"):
        assert not any(k.startswith(f"{board}.menu.rstpin.") for k in keys), board
        assert not any(k == f"{board}.build.rstpin_defines" for k in keys), board


def test_the_core_checks_only_with_the_define_and_never_stops_for_it():
    platform = (REPO / "platform.txt").read_text(encoding="utf-8")
    assert re.search(r"^build\.rstpin_defines=$", platform, re.M)
    for flags in ("compiler.c.flags", "compiler.cpp.flags", "compiler.S.flags"):
        assert "{build.rstpin_defines}" in re.search(rf"^{re.escape(flags)}=.*$", platform, re.M).group(0), flags
    src = (REPO / "cores/arduino/ch32rv_option_user.c").read_text(encoding="utf-8")
    check = src[src.index("void ch32rv_rstpin_check"):]
    check = check[:check.index("\n}\n")]
    assert "stop()" not in check
    crt0 = (REPO / "cores/arduino/crt0_ch32.S").read_text(encoding="utf-8")
    assert "#ifdef CH32RV_RSTPIN_USER" in crt0 and "call ch32rv_rstpin_check" in crt0
