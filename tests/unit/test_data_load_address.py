"""crt0_ch32.S copies .highcode and .data from _highcode_lma in one loop, so in flash they must sit as in RAM and
_data_lma must be where .data really loads. lld aligns an output section's load address as well as its address: with
an 8-byte object in .data, the image of 0.7.0 put .data 4 bytes past the copy's start and every initialised variable
came out shifted (CH32V307, 2026-11-12: a delivery's function pointer read 0 and the sketch jumped to address 0).
Linked here with the real linker scripts, after .text of every length mod 8."""
import json
import os
import pathlib
import re
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
CORE = REPO / "cores" / "arduino"


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


PROGRAM = r"""
#include <stdint.h>
void tick(void) {}
__attribute__((used)) volatile uint64_t big = 0x1122334455667788ull;
__attribute__((used)) void (*volatile fn)(void) = tick;
__attribute__((section(".vector"))) void _vector_base(void) { for (;;) { } }
/* .text grows by 4 bytes a step of PAD (a 4-byte nop each, compressed ones off). */
__attribute__((used)) void pad(void) { __asm__ volatile(".option push\n .option norvc\n .rept PAD\n nop\n .endr\n .option pop"); }
"""


@pytest.mark.parametrize("variant,ld,march,mabi", [
    ("CH32V003", "ch32v003_16k_2k.ld", "rv32ec_zicsr", "ilp32e"),
    ("CH32V307", "ch32v307_256k_64k.ld", "rv32imafc_zicsr", "ilp32f"),
])
@pytest.mark.parametrize("pad", [0, 1, 2, 3])
def test_data_loads_where_the_copy_starts(clang_bin, tmp_path, variant, ld, march, mabi, pad):
    src = tmp_path / "p.c"
    src.write_text(PROGRAM.replace(".rept PAD", f".rept {pad}"))
    elf = tmp_path / "p.elf"
    subprocess.run([str(clang_bin / "clang"), "--target=riscv32-unknown-elf", f"-march={march}", f"-mabi={mabi}",
                    "-Os", "-ffunction-sections", "-fdata-sections", "-nostdlib", "-fuse-ld=lld",
                    "-mno-relax", "-Wl,--gc-sections", "-Wl,-u,pad", "-Wl,-u,big", "-Wl,-u,fn", "-Wl,-e,_vector_base",
                    f"-L{CORE}", f"-L{REPO / 'variants' / variant}", f"-T{REPO / 'variants' / variant / ld}",
                    "-Wl,--defsym=__stack_size=512",    # platform.txt passes the Stack Size menu's this way
                    str(src), "-o", str(elf)], check=True, capture_output=True, text=True)
    heads = subprocess.run([str(clang_bin / "llvm-objdump"), "-h", str(elf)], capture_output=True,
                           text=True, check=True).stdout
    syms = subprocess.run([str(clang_bin / "llvm-nm"), str(elf)], capture_output=True, text=True,
                          check=True).stdout
    sym = {m.group(2): int(m.group(1), 16) for m in re.finditer(r"^([0-9a-f]+) \w (\S+)$", syms, re.M)}
    # Idx Name Size VMA LMA Type
    data = re.search(r"^\s*\d+\s+\.data\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)", heads, re.M)
    assert data and int(data.group(1), 16) >= 12, heads
    vma, lma = int(data.group(2), 16), int(data.group(3), 16)
    assert lma == sym["_data_lma"], (hex(lma), hex(sym["_data_lma"]))
    assert sym["_data_lma"] - sym["_highcode_lma"] == vma - sym["_highcode_vma_start"]
