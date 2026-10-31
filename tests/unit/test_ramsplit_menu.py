"""The Flash / RAM Split menu (boards.txt) against RM 32.4.6, Table 32-4.

The USER value and mask each entry hands ch32rv_option_user.c pick the split its
linker script is built for; a wrong pair would have the core set the chip to one
split and link the sketch for another. Only the parts the RM names get the
split's linker script; the others keep their own and no check at boot.
"""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]

# USER[7:5] -> (flash KB, RAM KB); 00x, 01x, 10x ignore bit 5
TABLE = {0b000: (192, 128), 0b001: (192, 128), 0b010: (224, 96), 0b011: (224, 96),
         0b100: (256, 64), 0b101: (256, 64), 0b110: (128, 192), 0b111: (288, 32)}
SPLIT_PARTS = re.compile(r"CH32V(303[RV]C|307[RWV]C|317[VWS]C)")


def boards() -> dict:
    out = {}
    for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep and not line.startswith("#"):
            out[key] = value
    return out


def test_each_entry_sets_the_split_its_sizes_and_linker_script_name():
    b = boards()
    entries = {k.removesuffix(".build.ramsplit_ld") for k in b if ".menu.ramsplit." in k and k.endswith(".build.ramsplit_ld")}
    assert {e.split(".")[0] for e in entries} == {"CH32V303", "CH32V307", "CH32V317"}
    for e in entries:
        board = e.split(".")[0]
        m = re.fullmatch(r"-DCH32RV_RAMSPLIT_USER=0x([0-9a-f]{2})u -DCH32RV_RAMSPLIT_MASK=0x([0-9a-f]{2})u"
                         r" -DCH32RV_RAMSPLIT_RAM_KB=(\d+)",
                         b[e + ".build.ramsplit_user"])
        user, mask = int(m.group(1), 16), int(m.group(2), 16)
        flash, ram = int(b[e + ".upload.maximum_size"]), int(b[e + ".upload.maximum_data_size"])
        assert int(m.group(3)) * 1024 == ram, e
        # every USER[7:5] the mask lets through is the same split
        for bits, (f_kb, r_kb) in TABLE.items():
            if (bits << 5 ^ user) & mask == 0:
                assert (f_kb * 1024, r_kb * 1024) == (flash, ram), e
        assert b[e + ".build.ramsplit_ld"] == f"{flash // 1024}k_{ram // 1024}k"
        ld = REPO / "variants" / board / f"{board.lower()}_{b[e + '.build.ramsplit_ld']}.ld"
        text = ld.read_text(encoding="utf-8")
        assert f"LENGTH = {flash}" in text and f"LENGTH = {ram}" in text, ld


def test_only_the_parts_with_the_split_follow_the_menu():
    b = boards()
    for key, value in b.items():
        m = re.fullmatch(r"(CH32V3[01][37])\.menu\.pnum\.(\w+)\.build\.ldscript", key)
        if not m:
            continue
        pn = m.group(2)
        follows = "{build.ramsplit_ld}" in value
        defines = b.get(key.replace(".build.ldscript", ".build.ramsplit_defines"))
        if pn == "ANY":
            # ANY has it when every part of the series does
            parts = [k.split(".")[3] for k in b if re.fullmatch(rf"{m.group(1)}\.menu\.pnum\.\w+", k)
                     and k.split(".")[3] != "ANY"]
            assert follows == all(SPLIT_PARTS.match(p) for p in parts), key
        else:
            assert follows == bool(SPLIT_PARTS.match(pn)), key
        assert (defines == "{build.ramsplit_user}") == follows, key
        if not follows:
            assert key.replace(".build.ldscript", ".upload.maximum_size") in b, key


def test_the_default_build_defines_nothing_and_the_core_checks_only_with_the_define():
    platform = (REPO / "platform.txt").read_text(encoding="utf-8")
    assert re.search(r"^build\.ramsplit_defines=$", platform, re.M)
    src = (REPO / "cores/arduino/ch32rv_option_user.c").read_text(encoding="utf-8")
    assert "#if defined(CH32RV_RAMSPLIT_USER)" in src
    crt0 = (REPO / "cores/arduino/crt0_ch32.S").read_text(encoding="utf-8")
    assert "#ifdef CH32RV_RAMSPLIT_USER" in crt0 and "call ch32rv_ramsplit_check" in crt0
