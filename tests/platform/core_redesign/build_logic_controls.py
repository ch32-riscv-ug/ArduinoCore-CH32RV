#!/usr/bin/env python3
"""Explicit compile/link of P2 logic controls, not deployable firmware.

No hardware is accessed; MCU startup, peripheral drivers, protocol stacks, and
real interrupt synchronization are intentionally absent and reported as absent.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


REPO = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_name("logic_entry.cpp")
PROFILES = REPO / "planning/core-redesign/p2-inputs.json"


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    commands = []
    report = {"scope": "P2 instrumented logic only; not MCU firmware or complete application footprint",
              "status": "running", "results": [], "unverified": ["startup", "IRQ synchronization", "I2C", "USB-PD",
                    "shared bus", "timing", "stack peak", "heap peak", "full-application fit"]}

    def save():
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")

    def run(command):
        entry = {"command": [str(x) for x in command], "returncode": None}
        number = len(commands); commands.append(entry)
        def log(): (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        log()
        result = subprocess.run(entry["command"], cwd=out, text=True, capture_output=True)
        entry.update(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr); log()
        if result.returncode:
            raise RuntimeError(f"command {number} failed: {result.stderr}")
        return result.stdout

    save()
    try:
        compiler = args.compiler.resolve()
        prefix = str(compiler)[:-3]
        report["compiler_version"] = run([compiler, "--version"]).splitlines()[0]
        report["compiler_sha256"] = sha(compiler)
        inputs = [PROFILES, SOURCE, Path(__file__), REPO / "tests/unit/core_redesign/r_workloads.hpp",
                  REPO / "tests/unit/core_redesign/traces.json"]
        report["inputs"] = {}
        for path in inputs:
            relative = path.resolve().relative_to(REPO)
            dest = out / "inputs" / relative; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
            report["inputs"][str(relative)] = sha(path)
        profiles = json.loads(PROFILES.read_text())
        for profile in profiles["targets"]:
            for lto in (False, True):
                directory = out / f"{profile['id']}-lto{int(lto)}"; directory.mkdir()
                linker = directory / "logic.ld"
                linker.write_text(f"""ENTRY(p2_logic_entry)
MEMORY {{ FLASH (rx) : ORIGIN = 0, LENGTH = {profile['flash_bytes']}
         RAM (rw) : ORIGIN = 0x20000000, LENGTH = {profile['ram_bytes']} }}
SECTIONS {{
 .text : {{ KEEP(*(.text.entry)) *(.text*) *(.rodata*) }} > FLASH
 .init_array : {{ KEEP(*(.init_array*)) }} > FLASH
 .data : {{ *(.data*) *(.sdata*) *(.srodata*) }} > RAM AT> FLASH
 .bss (NOLOAD) : {{ *(.bss*) *(.sbss*) *(COMMON) }} > RAM
 /DISCARD/ : {{ *(.eh_frame*) }}
}}
""")
                flags = [f"-march={profile['march']}", f"-mabi={profile['abi']}", "-Os", "-std=c++17",
                         "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti",
                         "-fno-threadsafe-statics", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
                         "-fstack-usage"]
                if profile["id"] == "K0": flags.append("-DP2_KNOB")
                if lto: flags.append("-flto")
                obj = directory / "logic.o"; elf = directory / "logic.elf"
                run([compiler, *flags, "-c", SOURCE, "-o", obj])
                library = Path(run([compiler, *flags, "-print-libgcc-file-name"]).strip()).resolve()
                libc = Path(run([compiler, *flags, "-print-file-name=libc.a"]).strip()).resolve()
                run([compiler, *flags, "-nostdlib", "-Wl,--gc-sections", "-Wl,--no-warn-rwx-segments",
                     f"-T{linker}", f"-Wl,-Map,{directory/'logic.map'}", obj, "-lc", "-lgcc", "-o", elf])
                sections = run([prefix + "size", "-A", elf])
                (directory / "sections.txt").write_text(sections)
                sizes = {}
                for line in sections.splitlines():
                    fields = line.split()
                    if len(fields) == 3 and fields[0].startswith("."): sizes[fields[0]] = int(fields[1])
                run([prefix + "objcopy", "-O", "binary", elf, directory / "logic.bin"])
                for name, options in (("nm", ["-S", "--size-sort"]), ("objdump", ["-dr"])):
                    (directory / f"{name}.txt").write_text(run([prefix + name, *options, elf]))
                row = dict(target=profile["part"], workload=profile["id"], lto=lto, sections=sizes,
                           flash_image_bytes=(directory / "logic.bin").stat().st_size,
                           static_ram_bytes=sum(sizes.get(s, 0) for s in (".data", ".bss")),
                           libgcc_sha256=sha(library), libc_sha256=sha(libc),
                           elf_sha256=sha(elf), stack_peak_bytes=None)
                report["results"].append(row); save()
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
    finally:
        save()
        (out / "sha256.json").write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob("*"))
                if p.is_file() and p.name != "sha256.json"}, indent=2) + "\n")
    print(json.dumps({"status":report["status"], "builds":len(report["results"]), "error":report.get("error")}))
    return int(report["status"] != "passed")


if __name__ == "__main__":
    sys.exit(main())
