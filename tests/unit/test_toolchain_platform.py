"""The toolchain (ch32rv-toolchain) as platform.txt and boards.txt use it.

clang picks its C library build (picolibc, compiler-rt, libc++) from -march /
-mabi through lib/clang-runtimes/multilib.yaml; a board whose -march no
variant maps leaves the link without a C library. XW (Xwchc) goes only where
every core of the series has it: the first XW instruction stops a QingKe V3A
(CH32V103).
"""
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


PLATFORM = (REPO / "platform.txt").read_text(encoding="utf-8")
BOARDS = (REPO / "boards.txt").read_text(encoding="utf-8")


def board_arches() -> dict:
    """board -> (march with XW where it has it, mabi)."""
    march = dict(re.findall(r"^(\w+)\.build\.march=(\S+)$", BOARDS, re.M))
    mabi = dict(re.findall(r"^(\w+)\.build\.mabi=(\S+)$", BOARDS, re.M))
    xw = dict(re.findall(r"^(\w+)\.build\.march_xw=(\S*)$", BOARDS, re.M))
    assert march and set(march) == set(mabi)
    return {b: (march[b] + xw.get(b, ""), mabi[b]) for b in march}


def test_xw_only_where_the_cores_have_it():
    xw = set(re.findall(r"^(\w+)\.build\.march_xw=_xwchc$", BOARDS, re.M))
    assert "CH32V103" not in xw
    assert "CH32V003" in xw


def test_no_gcc_left():
    for text in (PLATFORM, BOARDS):
        assert "riscv-none-elf" not in text
        assert "xpack" not in text
        assert "build.toolchain" not in text


@pytest.mark.parametrize("march,mabi", sorted(set(board_arches().values())))
def test_every_board_arch_finds_its_libraries(clang_bin, march, mabi, tmp_path):
    """The driver's link line names a clang-runtimes variant's lib/ with the
    library built for this ISA: with XW where the board has it, without where
    it does not."""
    out = subprocess.run([str(clang_bin / "clang"), "--target=riscv32-unknown-elf", f"-march={march}",
                          f"-mabi={mabi}", "-###", "-x", "c", "-", "-o", str(tmp_path / "a.elf")],
                         input="", capture_output=True, text=True, check=True).stderr
    dirs = re.findall(r'"-L[^"]*clang-runtimes/([\w]+)/(\w+)/lib"', out)
    variants = {d for d in dirs if d[0] != "common"}
    assert len(variants) == 1, (march, mabi, dirs)
    (isa, abi), = variants
    assert abi == mabi
    assert isa.endswith("_xwchc") == ("_xwchc" in march), (march, isa)
