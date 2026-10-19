#!/usr/bin/env python3
"""Isolated link-cost experiment; no hardware, Arduino runtime, or scheduler.

Compare disabled calls, unresolved weak hooks, archive extraction, section GC,
and constructor registration. Output is NOT a core footprint or timing result.
Run explicitly with --compiler <riscv-none-elf-g++> --out <new directory>.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ARCHES = (
    ("rv32e", "rv32ec_zicsr", "ilp32e"),
    ("rv32i", "rv32imac_zicsr", "ilp32"),
)
ENTRY = r"""
extern "C" void user_step();
#if DISPATCH == 1
extern "C" void service_hook() __attribute__((weak));
#elif DISPATCH == 2
extern "C" void service_hook();
#endif
extern "C" __attribute__((section(".text.entry"))) void probe_entry() {
    for (;;) {
        user_step();
#if DISPATCH == 1
        if (service_hook) service_hook();
#elif DISPATCH == 2
        service_hook();
#endif
    }
}
"""
USER = r"""
volatile unsigned user_runs;
extern "C" void user_step() { ++user_runs; }
"""
SERVICE = r"""
unsigned char service_buffer[64];
volatile unsigned service_runs;
extern "C" void service_hook() {
    ++service_buffer[service_runs & 63u];
    ++service_runs;
}
#if REGISTER
void (*volatile registered_callback)();
__attribute__((constructor)) static void register_service() {
    registered_callback = service_hook;
}
#endif
"""
EMPTY = 'extern "C" void service_hook() {}\n'
LINKER = """
ENTRY(probe_entry)
MEMORY {
    FLASH (rx) : ORIGIN = 0, LENGTH = 16K
    RAM (rw) : ORIGIN = 0x20000000, LENGTH = 2K
}
SECTIONS {
    .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > FLASH
    .init_array : { KEEP(*(.init_array*)) } > FLASH
    .data : { *(.data*) *(.sdata*) *(.srodata*) } > RAM AT> FLASH
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) } > RAM
}
"""
# Tuple: name, dispatch selection, extra link input. No LTO, as in platform.txt.
CASES = (
    ("baseline", 0, None),
    ("disabled_with_unused_object", 0, "service.o"),
    ("disabled_with_unused_archive", 0, "service.a"),
    ("weak_undefined", 1, None),
    ("weak_archive_only", 1, "service.a"),
    ("weak_empty_definition", 1, "empty.o"),
    ("weak_active_object", 1, "service.o"),
    ("strong_active_archive", 2, "service.a"),
    ("constructor_retained_object", 0, "registered.o"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.name.endswith("g++") or not compiler.is_file():
        parser.error("--compiler must name an existing RISC-V g++ executable")
    prefix = str(compiler)[:-3]
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    commands = []

    def run(command):
        commands.append(command)
        result = subprocess.run(command, text=True, capture_output=True)
        # Write after every command so failed experiments remain inspectable.
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"Command failed: {command}\n{result.stdout}{result.stderr}")
        return result.stdout

    report = {
        "scope": "freestanding link-only probe; not executable MCU firmware",
        "compiler": run([str(compiler), "--version"]).splitlines()[0],
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
        "lto": False,
        "results": [],
    }
    for name, body in (("entry.cpp", ENTRY), ("user.cpp", USER),
                       ("service.cpp", SERVICE), ("empty.cpp", EMPTY),
                       ("probe.ld", LINKER)):
        (out / name).write_text(body)

    for tag, march, abi in ARCHES:
        arch_dir = out / tag
        arch_dir.mkdir()
        flags = [f"-march={march}", f"-mabi={abi}", "-Os", "-std=gnu++17",
                 "-ffunction-sections", "-fdata-sections", "-fno-exceptions",
                 "-fno-rtti", "-fno-threadsafe-statics", "-fno-unwind-tables",
                 "-fno-asynchronous-unwind-tables"]

        def compile_object(source, dest, defines=()):
            run([str(compiler), *flags, *defines, "-c", str(out / source),
                 "-o", str(arch_dir / dest)])

        compile_object("user.cpp", "user.o")
        compile_object("service.cpp", "service.o", ["-DREGISTER=0"])
        compile_object("service.cpp", "registered.o", ["-DREGISTER=1"])
        compile_object("empty.cpp", "empty.o")
        run([prefix + "gcc-ar", "rcs", str(arch_dir / "service.a"),
             str(arch_dir / "service.o")])
        for dispatch in (0, 1, 2):
            compile_object("entry.cpp", f"entry{dispatch}.o", [f"-DDISPATCH={dispatch}"])

        baseline = None
        for case, dispatch, extra in CASES:
            elf = arch_dir / f"{case}.elf"
            inputs = [str(arch_dir / f"entry{dispatch}.o"), str(arch_dir / "user.o")]
            if extra:
                inputs.append(str(arch_dir / extra))
            run([str(compiler), *flags, "-nostdlib", "-Wl,--gc-sections",
                 "-Wl,--no-warn-rwx-segments", f"-T{out / 'probe.ld'}",
                 f"-Wl,-Map,{arch_dir / (case + '.map')}", *inputs, "-o", str(elf)])
            sections = run([prefix + "size", "-A", str(elf)])
            (arch_dir / f"{case}.sections.txt").write_text(sections)
            sizes = {}
            for line in sections.splitlines():
                fields = line.split()
                if len(fields) == 3 and fields[0].startswith("."):
                    sizes[fields[0]] = int(fields[1])
            flash_sections = sum(sizes.get(s, 0) for s in (".text", ".init_array", ".data"))
            binary = arch_dir / f"{case}.bin"
            run([prefix + "objcopy", "-O", "binary", str(elf), str(binary)])
            # Count image alignment gaps too, not just the sum of section sizes.
            flash = binary.stat().st_size
            ram = sum(sizes.get(s, 0) for s in (".data", ".bss"))
            if baseline is None:
                baseline = flash, ram
            for tool, options, suffix in (("nm", ["-S", "--size-sort"], "symbols.txt"),
                                           ("objdump", ["-dr"], "disasm.txt")):
                (arch_dir / f"{case}.{suffix}").write_text(run([prefix + tool, *options, str(elf)]))
            row = dict(arch=tag, march=march, abi=abi, case=case, sections=sizes,
                       flash_bytes=flash, flash_section_bytes=flash_sections, static_ram_bytes=ram,
                       delta_flash=flash - baseline[0], delta_ram=ram - baseline[1])
            report["results"].append(row)
            print(f"{tag:6} {case:34} flash={flash:4} RAM={ram:3} "
                  f"delta=({row['delta_flash']:+d}, {row['delta_ram']:+d})")
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
