"""The default pins of Serial / Wire / SPI, as the compiler sees them for each part number, against what that part
bonds (device-data's pinout.csv).

generate.py chooses one default route per instance for the whole series; a part that does not bond that route, or
has one of its pads as its own NRST / SWD / BOOT, gets its own default (emit_misfit_defaults). Here every
`<board>.menu.pnum.<part>` is preprocessed with its CH32RV_PART_ define, and every default pad must be a pad of that
part and not one of its debug or strap pads (V006E8R6 / E8R7 and V005E6R6 have NRST on PC5, SPI1's series-wide SCK;
the small X035 packages do not bond PA10 / PA11, I2C1's)."""
import csv
import json
import os
import pathlib
import re
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


@pytest.fixture(scope="module")
def clang_cmd(clang_bin) -> list:
    """clang for the target (rv32imac with XW, as CH32X035 / V20x are built), ready for the source's flags."""
    return [str(clang_bin / "clang"), "--target=riscv32-unknown-elf", "-march=rv32imac_zicsr_xwchc", "-mabi=ilp32"]


TABLES = REPO / ".tools" / "ch32-device-data" / "index" / "pinout.csv"
ROLES = {"SERIAL": ("TX", "RX"), "I2C": ("SCL", "SDA"), "SPI": ("SCK", "MISO", "MOSI")}


@pytest.fixture(scope="module")
def pinout():
    """part -> (its pads, its NRST / SWD / BOOT pads)."""
    if not TABLES.exists():
        pytest.skip("ch32-device-data is not fetched (.tools/ch32-device-data)")
    pads, forbidden = {}, {}
    with TABLES.open(encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if not r["port"] or not r["gpio"]:
                continue
            pad = f"P{r['port']}{int(r['gpio'])}"
            pads.setdefault(r["part_number"], set()).add(pad)
            if r["peripheral"] == "SDI" or (r["peripheral"] == "SYS" and r["role"] in ("NRST", "BOOT0", "BOOT1")):
                forbidden.setdefault(r["part_number"], set()).add(pad)
    return pads, forbidden


def parts():
    """(board, series, variant, part) for every part number of every board."""
    keys = dict(line.split("=", 1) for line in (REPO / "boards.txt").read_text(encoding="utf-8").splitlines()
                if "=" in line and not line.startswith("#"))
    out = []
    for key, value in keys.items():
        m = re.fullmatch(r"(\w+)\.menu\.pnum\.(\w+)\.build\.part", key)
        if m and value != "ANY":
            board = m.group(1)
            out.append((board, keys[f"{board}.build.series"], keys[f"{board}.build.variant"], value))
    return out


def defaults(clang_cmd, series, variant, part, tmp_path):
    """{'SPI1_SCK': 'PC5', ...}: the default pads the variant gives this part (the macros as they stand at the end,
    clang -dM)."""
    source = tmp_path / "probe.c"
    source.write_text("#include \"pins_arduino.h\"\n")
    result = subprocess.run([*clang_cmd, "-E", "-dM",
                             f"-DCH32RV_SERIES_{series}", f"-DCH32RV_PART_{part}",
                             "-I", str(REPO / "variants" / variant), "-I", str(REPO / "cores" / "arduino"),
                             str(source)], capture_output=True, text=True, check=True)
    wanted = "|".join(f"{kind}\\d+_(?:{'|'.join(roles)})" for kind, roles in ROLES.items())
    found, absent = {}, set()
    for line in result.stdout.splitlines():
        m = re.fullmatch(rf"#define CH32RV_({wanted}) (\S+)", line.strip())
        if m:
            found[m.group(1)] = m.group(2)
        m = re.fullmatch(r"#define CH32RV_(\w+?\d+)_ABSENT 1", line.strip())
        if m:
            absent.add(m.group(1))
    # An instance the part does not have (ch32rv_instances.h leaves it out) keeps the series' names unused.
    return {name: pad for name, pad in found.items() if name.rsplit("_", 1)[0] not in absent}


def test_every_default_pad_is_bonded_and_not_a_debug_or_strap_pad(clang_cmd, pinout, tmp_path):
    pads, forbidden = pinout
    wrong = []
    checked = 0
    for board, series, variant, part in parts():
        if part not in pads:
            continue                       # a board-fixed part device-data names differently
        for name, value in defaults(clang_cmd, series, variant, part, tmp_path).items():
            pad = value
            if not re.fullmatch(r"P[A-K]\d+", pad):
                continue                   # not a pad name
            checked += 1
            if pad not in pads[part]:
                wrong.append(f"{part} {name} {pad}: not bonded")
            elif pad in forbidden.get(part, ()):
                wrong.append(f"{part} {name} {pad}: its own debug / strap pad")
    assert checked > 500
    assert not wrong, "\n".join(wrong)
