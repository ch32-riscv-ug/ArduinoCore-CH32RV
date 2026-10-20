#!/usr/bin/env python3
"""EXP-02 serial-data code/storage matrix; link-only, no hardware or cycle claims."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_name("data_entry.cpp")
HEADER = REPO / "tests/unit/core_redesign/data_parts.hpp"


def sha(path):
    with Path(path).open("rb") as f: return hashlib.file_digest(f, "sha256").hexdigest()


def cases(smoke):
    result = [("baseline", 0, 4, 4, 0, 1), ("unused-queue", 1, 4, 4, 0, 0)]
    if smoke: return result
    result.extend([("unused-view", 1, 4, 4, 1, 0), ("unused-wake", 1, 4, 4, 2, 0),
                   ("unused-ring", 2, 64, 1, 0, 0), ("unused-message", 3, 32, 8, 0, 0),
                   ("unused-semaphore", 4, 4, 1, 0, 0), ("unused-bits", 5, 1, 1, 0, 0),
                   ("unused-lock", 6, 1, 1, 0, 0), ("unused-pool", 7, 4, 16, 0, 0)])
    for n in (1, 4, 16):
        for width in (1, 4, 16):
            for variant, label in enumerate(("fixed", "borrowed", "wake")):
                result.append((f"queue-{n}x{width}-{label}", 1, n, width, variant, 1))
    for n in (16, 64, 256): result.append((f"ring-{n}", 2, n, 1, 0, 1))
    result.extend([("message-32-8", 3, 32, 8, 0, 1), ("message-128-32", 3, 128, 32, 0, 1),
                   ("semaphore-1", 4, 1, 1, 0, 1), ("semaphore-4", 4, 4, 1, 0, 1),
                   ("bits", 5, 1, 1, 0, 1), ("lock", 6, 1, 1, 0, 1)])
    for width in (16, 64):
        for n in (1, 4): result.append((f"pool-{n}x{width}", 7, n, width, 0, 1))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--smoke", action="store_true", help="baseline and unused queue only")
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve(); compiler = args.compiler.resolve(); prefix = str(compiler)[:-3]
    commands = []
    report = {"scope":"serial context, instrumented link-only; no runtime/IRQ guarantees",
              "status":"running", "results":[], "input_hashes":{}}

    def save(): (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    def run(command):
        entry = {"command":[str(p) for p in command], "returncode":None}; commands.append(entry)
        def log(): (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        log()
        result = subprocess.run(entry["command"], cwd=out, capture_output=True, text=True)
        entry.update(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr); log()
        if result.returncode: raise RuntimeError(f"{entry['command']}: {result.stderr}")
        return result.stdout
    save()
    try:
        for source in (SOURCE, HEADER, Path(__file__).resolve(), REPO / "tools/index/tools_xpack_gcc.json"):
            relative = source.relative_to(REPO); target = out / "inputs" / relative
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
            report["input_hashes"][str(relative)] = sha(source)
        report["compiler"] = run([compiler,"--version"]).splitlines()[0]
        report["compiler_sha256"] = sha(compiler)
        report["tool_hashes"] = {}
        for name in ("cc1plus", "as", "ld", "collect2"):
            path = Path(run([compiler, f"-print-prog-name={name}"]).strip()).resolve()
            report["tool_hashes"][name] = sha(path)
        for name in ("size", "objcopy", "nm", "objdump"):
            report["tool_hashes"][name] = sha(Path(prefix + name))
        report["libraries"] = {}
        for arch, march, abi in (("rv32e","rv32ec_zicsr","ilp32e"),("rv32i","rv32imac_zicsr","ilp32")):
            report["libraries"][arch] = {}
            for name, query in (("libgcc", "-print-libgcc-file-name"), ("libc", "-print-file-name=libc.a")):
                path = Path(run([compiler, f"-march={march}", f"-mabi={abi}", query]).strip()).resolve()
                report["libraries"][arch][name] = sha(path)
            for lto in (False, True):
                baseline = None
                for label, part, capacity, width, variant, used in cases(args.smoke):
                    dest = out / f"{arch}-lto{int(lto)}" / label; dest.mkdir(parents=True)
                    ld = dest / "data.ld"
                    ld.write_text("""ENTRY(data_entry)
MEMORY { FLASH (rx) : ORIGIN = 0, LENGTH = 16K
 RAM (rw) : ORIGIN = 0x20000000, LENGTH = 2K }
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > FLASH
 .init_array : { KEEP(*(.init_array*)) } > FLASH
 .data : { *(.data*) *(.sdata*) *(.srodata*) } > RAM AT> FLASH
 .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) } > RAM
}
""")
                    flags=[f"-march={march}",f"-mabi={abi}","-Os","-std=c++17","-ffunction-sections",
                           "-fdata-sections","-fno-exceptions","-fno-rtti","-fno-threadsafe-statics",
                           "-fno-unwind-tables","-fno-asynchronous-unwind-tables","-fstack-usage"]
                    if lto: flags.append("-flto")
                    defines=[f"-DPART={part}",f"-DCAPACITY={capacity}",f"-DWIDTH={width}",
                             f"-DVARIANT={variant}",f"-DUSED={used}"]
                    obj=dest/"data.o"; elf=dest/"data.elf"
                    run([compiler,*flags,*defines,"-c",SOURCE,"-o",obj])
                    run([compiler,*flags,"-nostdlib","-Wl,--gc-sections","-Wl,--no-warn-rwx-segments",
                         f"-T{ld}",f"-Wl,-Map,{dest/'data.map'}",obj,"-lc","-lgcc","-o",elf])
                    section_text=run([prefix+"size","-A",elf]); (dest/"sections.txt").write_text(section_text)
                    sections={}
                    for line in section_text.splitlines():
                        fields=line.split()
                        if len(fields)==3 and fields[0].startswith("."): sections[fields[0]]=int(fields[1])
                    run([prefix+"objcopy","-O","binary",elf,dest/"data.bin"])
                    symbols=run([prefix+"nm","-S","--size-sort",elf]); (dest/"symbols.txt").write_text(symbols)
                    (dest/"disasm.txt").write_text(run([prefix+"objdump","-dr",elf]))
                    flash=(dest/"data.bin").stat().st_size; ram=sections.get(".data",0)+sections.get(".bss",0)
                    if baseline is None: baseline=(flash,ram)
                    report["results"].append(dict(arch=arch,lto=lto,case=label,flash_bytes=flash,
                        static_ram_bytes=ram,delta_flash=flash-baseline[0],delta_ram=ram-baseline[1],
                        init_array_bytes=sections.get(".init_array",0),sections=sections,elf_sha256=sha(elf)))
                    save()
        report["unused_zero"] = all(r["delta_flash"] == r["delta_ram"] == r["init_array_bytes"] == 0
                                    for r in report["results"] if r["case"].startswith("unused-"))
        report["status"]="passed"
    except Exception as error: report.update(status="failed",error=str(error))
    finally:
        save()
        (out/"sha256.json").write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob("*"))
            if p.is_file() and p.name!="sha256.json"},indent=2)+"\n")
    print(json.dumps({"status":report["status"],"builds":len(report["results"]),"error":report.get("error")}))
    return int(report["status"]!="passed")

if __name__=="__main__": sys.exit(main())
