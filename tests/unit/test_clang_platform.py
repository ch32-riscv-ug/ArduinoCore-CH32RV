"""The Compiler menu's Clang: what platform.txt hard-codes about the GCC install.

clang compiles against newlib's headers and links newlib-nano and libgcc from
the xPack GCC (platform.txt, the compiler section), and its driver cannot pick
xPack's multilib by itself, so platform.txt names, per -march/-mabi, the
directory GCC's own driver would take, and the GCC version its directories are
named by. Both go stale silently when a board gets a new -march or the GCC
fragment moves to a new release: the link then takes the wrong libc (or none).
"""
import json
import pathlib
import re
import shutil
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
PLATFORM = (REPO / "platform.txt").read_text(encoding="utf-8")
BOARDS = (REPO / "boards.txt").read_text(encoding="utf-8")


def board_arch_pairs() -> set:
    march = dict(re.findall(r"^(\w+)\.build\.march=(\S+)$", BOARDS, re.M))
    mabi = dict(re.findall(r"^(\w+)\.build\.mabi=(\S+)$", BOARDS, re.M))
    assert march and set(march) == set(mabi)
    return {(march[b], mabi[b]) for b in march}


def multilibs() -> dict:
    return {(m, a): d for m, a, d in
            re.findall(r"^compiler\.multilib\.([\w]+)\.([\w]+)=(\S+)$", PLATFORM, re.M)}


def gcc_bin() -> pathlib.Path | None:
    frag = json.loads((REPO / "tools/index/tools_xpack_gcc.json").read_text(encoding="utf-8"))
    for exe in (REPO / ".tools" / frag["name"] / frag["version"] / "bin" / "riscv-none-elf-gcc",
                pathlib.Path(shutil.which("riscv-none-elf-gcc") or "/nonexistent")):
        if exe.exists():
            return exe
    return None


def test_every_board_arch_has_a_multilib():
    missing = board_arch_pairs() - set(multilibs())
    assert not missing, f"add compiler.multilib.<march>.<mabi> to platform.txt for {sorted(missing)}"


def test_gcc_version_matches_the_fragment():
    frag = json.loads((REPO / "tools/index/tools_xpack_gcc.json").read_text(encoding="utf-8"))
    declared = re.search(r"^compiler\.gcc\.version=(\S+)$", PLATFORM, re.M).group(1)
    # xPack's version is <GCC version>-<build>: 15.2.0-1 -> 15.2.0
    assert frag["version"].rsplit("-", 1)[0] == declared


@pytest.mark.skipif(gcc_bin() is None, reason="no riscv-none-elf-gcc (tools/index/fetch_tools.py)")
@pytest.mark.parametrize("march,mabi", sorted(board_arch_pairs()))
def test_multilib_is_gccs_own_choice(march, mabi):
    out = subprocess.run([str(gcc_bin()), f"-march={march}", f"-mabi={mabi}", "-print-multi-directory"],
                         capture_output=True, text=True, check=True).stdout.strip()
    assert multilibs()[(march, mabi)] == out


def test_xw_only_where_the_cores_have_it():
    xw = set(re.findall(r"^(\w+)\.build\.march_xw=_xwchc$", BOARDS, re.M))
    # QingKe V3A (ch32-device-data cores.csv xw=no): the first XW instruction stops it.
    assert "CH32V103" not in xw
    assert "CH32V003" in xw


def test_every_board_has_the_compiler_menu():
    boards = set(re.findall(r"^(\w+)\.name=", BOARDS, re.M))
    for b in boards:
        assert f"{b}.menu.compiler.gcc.build.toolchain=gcc" in BOARDS, b
        assert f"{b}.menu.compiler.clang.build.toolchain=clang" in BOARDS, b


def test_recipes_take_only_the_picked_names():
    # A recipe naming compiler.gcc.* / compiler.clang.* directly would ignore the menu.
    for line in PLATFORM.splitlines():
        if line.startswith("recipe."):
            assert "{compiler.gcc." not in line and "{compiler.clang." not in line, line
