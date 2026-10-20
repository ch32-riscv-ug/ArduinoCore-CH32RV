#!/usr/bin/env python3
"""Core-redesign component matrices; link-only, no hardware or cycle claims."""
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


def shared_cases(smoke):
    result = [("baseline", 0, 4, 16, 0, 1), ("unused-owned-manual", 1, 4, 16, 0, 0)]
    if smoke: return result
    result.extend([("unused-irq-v003", 2, 4, 16, 0, 0),
                   ("unused-irq-x035", 2, 4, 16, 1, 0), ("unused-spsc", 3, 4, 16, 0, 0)])
    for n in (1, 4, 16):
        result.extend([(f"owned-manual-{n}", 1, n, 16, 0, 1),
                       (f"irq-v003-{n}", 2, n, 16, 0, 1),
                       (f"irq-x035-{n}", 2, n, 16, 1, 1),
                       (f"spsc-{n}", 3, n, 16, 0, 1)])
    return result


def work_cases(smoke):
    result = [("baseline", 0, 4, 4, 0, 1), ("unused-work", 1, 4, 4, 0, 0)]
    if smoke: return result
    for n in (1, 4, 8):
        for quota in (1, 4, 8): result.append((f"work-{n}-quota{quota}", 1, n, quota, 0, 1))
    for variant, label in ((1, "S"), (2, "U"), (3, "M")):
        result.append((f"delay-{label}-4-quota4", 1, 4, 4, variant, 1))
    return result


def io_cases(smoke):
    result = [("baseline", 0, 32, 1, 0, 1), ("unused-buffer", 1, 32, 1, 0, 0)]
    if smoke: return result
    result.append(("unused-slot", 2, 32, 1, 0, 0))
    for n in (8, 32, 128):
        result.extend([(f"buffer-{n}", 1, n, 1, 0, 1),
                       (f"slot-{n}", 2, n, 1, 0, 1), (f"two-slots-{n}", 2, n, 1, 1, 1)])
    return result


def timer_cases(smoke):
    result = [("baseline", 0, 1, 1, 0, 1), ("unused-timer", 1, 1, 1, 0, 0)]
    if not smoke:
        result.extend([(label, 1, 1, 1, variant, 1) for variant, label in
                       enumerate(("runtime-period", "oneshot", "period10"))])
    return result


def event_cases(smoke):
    result = [("baseline", 0, 4, 4, 0, 1), ("unused-event", 1, 4, 4, 0, 0)]
    if smoke: return result
    result.append(("unused-bridge", 2, 4, 4, 0, 0))
    for n in (1, 4, 8):
        for capacity in (1, 4, 8): result.append((f"event-{n}-mailbox{capacity}-quota4", 1, n, capacity, 0, 1))
    for variant, label in ((1, "delay-S"), (2, "delay-U"), (3, "delay-M"), (4, "quota1"), (5, "quota8")):
        result.append((f"event-4-mailbox4-{label}", 1, 4, 4, variant, 1))
    for n, capacity in ((1, 1), (4, 4)): result.append((f"bridge-{n}-mailbox{capacity}", 2, n, capacity, 0, 1))
    return result


def k0_cases(smoke):
    result = [("baseline", 0, 1, 0, 0, 1), ("unused-R", 1, 1, 0, 0, 0)]
    if smoke: return result
    for part, candidate in enumerate(("R", "A", "B", "C"), start=1):
        if part > 1: result.append((f"unused-{candidate}", part, 1, 0, 0, 0))
        for capacity in (1, 2):
            for profile, label in enumerate(("M-explicit", "S-delay", "U-delay")):
                result.append((f"{candidate}-capacity{capacity}-{label}", part, capacity, profile, 0, 1))
    return result


def wait_cases(smoke):
    result = [("baseline", 0, 1, 1, 0, 1), ("unused-wrapper", 1, 1, 1, 0, 0)]
    if not smoke:
        result.extend([("unused-async", 2, 1, 1, 0, 0), ("try", 3, 1, 1, 0, 1),
                       ("async", 2, 1, 1, 0, 1), ("blocking", 1, 1, 1, 0, 1)])
    return result


def x0_cases(smoke):
    result = [("baseline", 0, 4, 0, 0, 1), ("unused-R", 1, 4, 0, 0, 0)]
    if not smoke:
        for part, candidate in enumerate(("R", "A", "B", "C"), 1):
            if part != 1: result.append((f"unused-{candidate}", part, 4, 0, 0, 0))
            for profile, label in enumerate(("M", "S", "U")):
                for touch in (0, 1):
                    result.append((f"{candidate}-{label}-touch{touch}", part, 4, profile, touch, 1))
    return result


def dependency_cases(smoke):
    result = [("baseline", 0, 4, 3, 0, 1), ("unused-graph", 1, 4, 3, 0, 0)]
    if not smoke:
        result.append(("unused-guard", 2, 4, 3, 0, 0))
        for actors, resources in ((2, 1), (4, 3), (8, 3), (16, 16)):
            result.append((f"inspect-{actors}x{resources}", 1, actors, resources, 0, 1))
        result.extend([("guarded-wait-4x3", 2, 4, 3, 0, 1), ("plain-wait", 3, 4, 3, 0, 1)])
    return result


def x0_feature_cases(smoke):
    result = [("baseline", 0, 4, 0, 2, 1), ("unused-R-off", 1, 4, 0, 2, 0)]
    if not smoke:
        for part, candidate in enumerate(("R", "A", "B", "C"), 1):
            for variant, mode in ((2, "off"), (3, "on")):
                if part != 1 or variant != 2: result.append((f"unused-{candidate}-{mode}", part, 4, 0, variant, 0))
            for variant, mode in enumerate(("runtime0", "runtime1", "off", "on", "manual")):
                result.append((f"{candidate}-{mode}", part, 4, 0, variant, 1))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--smoke", action="store_true", help="baseline and one unused object only")
    parser.add_argument("--matrix", choices=("serial", "shared", "work", "io", "timer", "event", "k0", "wait", "x0", "dependencies", "x0-features"), default="serial")
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve(); compiler = args.compiler.resolve(); prefix = str(compiler)[:-3]
    commands = []
    report = {"scope":"serial context, instrumented link-only; no runtime/IRQ guarantees",
              "status":"running", "results":[], "input_hashes":{}}
    source = SOURCE
    inputs = [SOURCE, HEADER]
    selected_cases = cases(args.smoke)
    if args.matrix == "shared":
        source = Path(__file__).with_name("shared_entry.cpp")
        inputs = [source, HEADER, source.with_name("irq_gate.hpp"),
                  HEADER.with_name("owned_queue.hpp"), HEADER.with_name("shared_channels.hpp")]
        selected_cases = shared_cases(args.smoke)
        report["scope"] = "lifetime, IRQ-gate assembly and SPSC link-only; no runtime/IRQ/cache guarantees"
    elif args.matrix == "work":
        source = Path(__file__).with_name("work_entry.cpp")
        inputs = [source, HEADER.with_name("work_executor.hpp")]
        selected_cases = work_cases(args.smoke)
        report["scope"] = "RUN-01/02 coalesced-work preparation, link-only; no I/O, timer, stack peak or cycle proof"
    elif args.matrix == "io":
        source = Path(__file__).with_name("io_entry.cpp")
        inputs = [source, HEADER.with_name("io_lifecycle.hpp")]
        selected_cases = io_cases(args.smoke)
        report["scope"] = "IO-01/04 lease/state model link-only; no physical I/O, startup, ISR or cycle proof"
    elif args.matrix == "timer":
        source = Path(__file__).with_name("timer_entry.cpp")
        inputs = [source, HEADER.with_name("deadline_timer.hpp")]
        selected_cases = timer_cases(args.smoke)
        report["scope"] = "TIME-01/02 deadline model link-only; no hardware clock, sleep, tick or cycle proof"
    elif args.matrix == "event":
        source = Path(__file__).with_name("event_entry.cpp")
        inputs = [source, HEADER, HEADER.with_name("work_executor.hpp"), HEADER.with_name("event_executor.hpp"),
                  HEADER.with_name("io_lifecycle.hpp"), HEADER.with_name("deadline_timer.hpp")]
        selected_cases = event_cases(args.smoke)
        report["scope"] = "targeted event and I/O/timer model link-only; no physical protocol, startup, IRQ or cycle proof"
    elif args.matrix == "k0":
        source = Path(__file__).with_name("k0_entry.cpp")
        inputs = [source, HEADER, HEADER.with_name("work_executor.hpp"), HEADER.with_name("event_executor.hpp"),
                  HEADER.with_name("k0_candidates.hpp")]
        selected_cases = k0_cases(args.smoke)
        report["scope"] = "K0 equivalent R/A/B/C logic link-only; no physical I2C, startup, ISR atomicity or cycle proof"
    elif args.matrix == "wait":
        source = Path(__file__).with_name("wait_entry.cpp")
        inputs = [source, HEADER.with_name("work_executor.hpp"), HEADER.with_name("wait_policy.hpp")]
        selected_cases = wait_cases(args.smoke)
        report["scope"] = "RUN-03 wait policy/continuation link-only with toy predicate; no dispatcher, queue, physical producer or cycle proof"
    elif args.matrix == "x0":
        source = Path(__file__).with_name("x0_entry.cpp")
        inputs = [source, HEADER, HEADER.with_name("work_executor.hpp"), HEADER.with_name("event_executor.hpp"),
                  HEADER.with_name("static_mailbox.hpp"), HEADER.with_name("x0_candidates.hpp"),
                  HEADER.with_name("r_application.hpp"), HEADER.with_name("r_workloads.hpp")]
        selected_cases = x0_cases(args.smoke)
        report["scope"] = "X0 equivalent R/A/B/C-static-batch logic link-only; runtime touch flag; no physical PD/I2C, startup, ISR, stack peak or cycle proof"
    elif args.matrix == "dependencies":
        source = Path(__file__).with_name("dependency_entry.cpp")
        inputs = [source, HEADER.with_name("work_executor.hpp"), HEADER.with_name("wait_policy.hpp"),
                  HEADER.with_name("wait_dependencies.hpp")]
        selected_cases = dependency_cases(args.smoke)
        report["scope"] = "bounded declared dependency inspection/Drive guard link-only with toy predicate; no I/O, ownership discovery, dispatcher, atomic snapshot or cycle proof"
    elif args.matrix == "x0-features":
        source = Path(__file__).with_name("x0_features_entry.cpp")
        inputs = [source, HEADER, HEADER.with_name("work_executor.hpp"), HEADER.with_name("event_executor.hpp"),
                  HEADER.with_name("static_mailbox.hpp"), HEADER.with_name("x0_candidates.hpp"),
                  HEADER.with_name("r_application.hpp"), HEADER.with_name("r_workloads.hpp"), HEADER.with_name("x0_features.hpp")]
        selected_cases = x0_feature_cases(args.smoke)
        report["scope"] = "X0 fixed absent/present touch versus hand-stripped and historical runtime controls; M-only logic link; no physical PD/I2C, startup, ISR, stack peak or cycle proof"
    report["matrix"] = args.matrix

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
        for input_path in (*inputs, Path(__file__).resolve(), REPO / "tools/index/tools_xpack_gcc.json"):
            relative = input_path.relative_to(REPO); target = out / "inputs" / relative
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(input_path, target)
            report["input_hashes"][str(relative)] = sha(input_path)
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
            # Both IRQ policies are cross-compiled for inspection. X035's SDK
            # uses fence.i; the shared matrix needs its explicit ISA extension.
            if args.matrix == "shared": march += "_zifencei"
            report["libraries"][arch] = {}
            for name, query in (("libgcc", "-print-libgcc-file-name"), ("libc", "-print-file-name=libc.a")):
                path = Path(run([compiler, f"-march={march}", f"-mabi={abi}", query]).strip()).resolve()
                report["libraries"][arch][name] = sha(path)
            for lto in (False, True):
                baseline = None
                for label, part, capacity, width, variant, used in selected_cases:
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
                    run([compiler,*flags,*defines,"-c",source,"-o",obj])
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
