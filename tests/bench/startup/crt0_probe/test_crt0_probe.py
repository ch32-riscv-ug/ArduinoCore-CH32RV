"""Does our own crt0 hand setup() a correctly initialised RAM on this board?

ADR-0003 replaced WCH's startup with our own; the static evidence is the ELF equivalence in build/startup, this
is the dynamic one, per board: after the upload the probe halts the part, fills RAM from .data's start to a few
words past _ebss with a pattern, and resets it. A C++ global constructor - the first user code crt0 runs - records
what it saw, and the sketch reports it on RUN: .bss zeroed, .data copied from flash, .init_array run. The control
is the pattern still sitting past _ebss, which nothing initialises; without it "bss was zero" is no evidence on a
part whose RAM powers up at zero.

Filling after the upload matters: the flash algorithm itself uses RAM.

  uv run --env-file .env pytest bench/startup/crt0_probe --profile ch32x035 -s
"""
import pathlib
import re
import subprocess

import pytest
from loader import load

kit = load("tests/bench/bench_kit.py", "bench_kit")
tk = load("tests/bench/tracekit.py", "tracekit")
import benchdef  # noqa: E402  (bench/ is on sys.path: conftest)

PATTERN = 0xDEADBEEF
PAST_EBSS_WORDS = 4
MAX_FILL_WORDS = 16384         # 64 KiB: the largest RAM on the bench, with margin


def symbols(nm: pathlib.Path, elf: pathlib.Path) -> dict[str, int]:
    out = subprocess.run([str(nm), str(elf)], capture_output=True, text=True, check=True).stdout
    want = {"_data_vma", "_ebss", "_sdata", "_edata", "_sbss"}
    found = {}
    for line in out.splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)   # the type of a PROVIDEd symbol is "?"
        if m and m.group(2) in want:
            found[m.group(2)] = int(m.group(1), 16)
    if "_data_vma" not in found and "_sdata" in found:
        found["_data_vma"] = found["_sdata"]
    missing = {"_data_vma", "_ebss"} - set(found)
    assert not missing, f"{elf.name} lacks {sorted(missing)}: the linker script names them (ADR-0003)"
    return found


def test_crt0_hands_setup_an_initialised_ram(request, dut, bench, arduino_cli_app, arduino_cli_build_properties):
    if bench.probe.get("kind") != "oep":
        pytest.skip("filling RAM through the probe needs an OEP bench")
    kit.start(dut, "crt0_probe")
    oep_host = request.getfixturevalue("oep_host")
    from oep_client import riscv
    # Where the sketch's RAM is: from the ELF the plugin just built, with the platform's own nm.
    build = pathlib.Path(arduino_cli_app.build_path)
    elf = next(build.glob("*.ino.elf"))
    gcc = arduino_cli_build_properties.get("runtime.tools.xpack-riscv-none-elf-gcc.path")
    assert gcc, "the platform declares no toolchain path (runtime.tools.xpack-riscv-none-elf-gcc.path)"
    sym = symbols(pathlib.Path(gcc) / "bin" / "riscv-none-elf-nm", elf)
    first, last = sym["_data_vma"], sym["_ebss"] + 4 * PAST_EBSS_WORDS
    words = (last - first) // 4
    assert 0 < words <= MAX_FILL_WORDS, f"{words} words from {first:#x} to {last:#x} is not a RAM image this test fills"
    # Halt, fill, reset: through the probe, on a connection of our own.
    _wire, conn = benchdef.attach_slot(bench, oep_host.host, halt=True)
    dm = riscv.RiscvDm(oep_host.host, conn)
    print(f"filling {words} words from {first:#010x} with {PATTERN:#010X} (_ebss is {sym['_ebss']:#010x})")
    pattern = PATTERN.to_bytes(4, "little")
    # One write_block moves at most what the probe declares (describe max_length, oep-if-debug §4.5): 1000 bytes
    # on a P4, 488 on a classic ESP32. A longer one is refused as unsupported.
    chunk = dm.max_words
    for off in range(0, words, chunk):
        n = min(chunk, words - off)
        dm.write_block(first + 4 * off, pattern * n)
    assert dm.read_block(sym["_ebss"], 1) == pattern, "the fill did not land past _ebss"
    flags, pc = dm.reset(confirm=True)
    print(f"reset: flags={flags:#x} pc={pc:#x}")
    # The sketch is back; its constructor ran on the filled RAM.
    kit.start(dut, "crt0_probe")
    dut.write("RUN\n")
    seen = {}
    for name in ("data_at_ctor", "bss_at_ctor", "ctor", "past_ebss", "data_now", "bss_now"):
        m = dut.expect(re.compile(rb"%s=([0-9A-F]{8})\r?\n" % name.encode()), timeout=10)
        seen[name] = int(m.group(1), 16)
    print(" ".join(f"{k}={v:08X}" for k, v in seen.items()))
    dut.expect_exact("bss_zeroed PASS")
    dut.expect_exact("data_copied_from_flash PASS")
    dut.expect_exact("init_array_ran PASS")
    dut.expect_exact("crt0_probe done failures=0")
    # The control, host-side: the host owns the pattern, so the host compares it.
    assert seen["past_ebss"] == PATTERN, (
        f"past _ebss reads {seen['past_ebss']:#010X}, not the pattern: the fill did not reach the part or the "
        f"reset came before it, so the three PASS lines above prove nothing")
