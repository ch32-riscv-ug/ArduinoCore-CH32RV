#!/usr/bin/env python3
"""Explicit EXP-04 partition ELF/image checks; no core linker edit or upload."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec); spec.loader.exec_module(loaded)
    return loaded


regions = module("exp04_regions", ROOT / "flash_regions.py")
elf = module("exp04_elf", ROOT / "runtime/inspect_boot.py")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inspect_hex(text, image, start):
    address = 0; contents = {}; ended = False
    for line in text.splitlines():
        assert line.startswith(":"), "invalid hex record"
        record = bytes.fromhex(line[1:])
        assert len(record) == record[0] + 5 and sum(record) % 256 == 0
        assert not ended
        offset = int.from_bytes(record[1:3], "big"); kind = record[3]; data = record[4:-1]
        if kind == 0:
            for i, value in enumerate(data):
                assert address + offset + i not in contents
                contents[address + offset + i] = value
        elif kind in [2, 4]:
            assert len(data) == 2 and offset == 0
            address = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind == 1:
            assert not data and offset == 0; ended = True
        else:
            assert kind in [3, 5] and len(data) == 4
    assert ended and contents == {start + i: value for i, value in enumerate(image)}


def inspect(raw, image, symbols, partition):
    header, sections = elf.elf_sections(raw)
    p = partition; start = p["link_origin"] + p["app_offset"]
    assert header[4] == symbols["probe_main"] == start
    assert symbols["__persistent_start"] == p["storage_link_start"]
    assert symbols["__persistent_end"] == p["storage_link_end"]
    for name in ["start", "end"]:
        assert symbols["__persistent_physical_" + name] == p["storage_physical_" + name]
    assert symbols["__persistent_reserved_bytes"] == p["reserved_bytes"]
    store = sections.get(".persistent")
    if p["reserved_bytes"]:
        assert store and store["type"] == 8 and store["size"] == p["reserved_bytes"]
        assert store["address"] == p["storage_link_start"] and not store["data"]
    else:
        assert not store or store["size"] == 0
    # Program headers describe actual file-backed bytes, not just section names.
    load_ranges = []
    for i in range(header[10]):
        ph = struct.unpack_from("<8I", raw, header[5] + i * header[9])
        kind, offset, vma, lma, filesz, memsz, flags, align = ph
        if kind != 1 or not filesz:
            continue
        assert start <= lma and lma + filesz <= p["storage_link_start"]
        assert not (p["reserved_bytes"] and lma < p["storage_link_end"] and p["storage_link_start"] < lma + filesz)
        load_ranges.append([lma, lma + filesz])
        assert image[lma - start:lma - start + filesz] == raw[offset:offset + filesz]
    assert load_ranges and len(image) == symbols["__app_load_end"] - start
    assert len(image) <= p["app_bytes"]
    # The initialized RAM word must remain in the Flash load image.
    assert sections[".data"]["data"] == bytes.fromhex("78563412")
    return dict(image_bytes=len(image),data_bytes=sections[".data"]["size"],
                bss_bytes=sections[".bss"]["size"],file_backed_load_ranges=load_ranges,
                reserved_bytes=p["reserved_bytes"],app_capacity_bytes=p["app_bytes"],
                storage_link_start=p["storage_link_start"],storage_physical_start=p["storage_physical_start"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sources", type=Path)
    args = parser.parse_args(); args.out.mkdir(parents=True, exist_ok=False); out = args.out.resolve()
    compiler = args.compiler.resolve(); prefix = str(compiler)[:-3]
    config = json.loads(regions.MANIFEST.read_text())
    report = dict(status="running",scope=__doc__,results=[],negative_checks=[],inputs={})
    commands = []

    def save():
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")

    def run(command, expect=True):
        item = dict(command=[str(s) for s in command],expected_success=expect,returncode=None)
        commands.append(item); save()
        p = subprocess.run(item["command"], cwd=out, capture_output=True, text=True)
        item.update(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr); save()
        if (p.returncode == 0) != expect:raise RuntimeError(str(item))
        return p

    try:
        for source in [Path(__file__).resolve(), ROOT / "flash_regions.py", ROOT / "runtime/inspect_boot.py", regions.MANIFEST]:
            relative = source.relative_to(REPO); target = out / "inputs" / relative
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
            report["inputs"][str(relative)] = sha(source)
        refs = out / "references"; refs.mkdir()
        for source in config["sources"]:
            dest = refs / source["file"]
            if args.sources:shutil.copyfile(args.sources / source["file"], dest)
            else:dest.write_bytes(urllib.request.urlopen(source["url"], timeout=45).read())
            assert dest.stat().st_size == source["bytes"] and sha(dest) == source["sha256"]
        report["compiler"] = run([compiler, "--version"]).stdout.splitlines()[0]
        report["compiler_sha256"] = sha(compiler); report["tool_hashes"] = {}
        for name in ["cc1plus", "as", "ld", "collect2"]:
            path = Path(run([compiler, f"-print-prog-name={name}"]).stdout.strip()).resolve()
            report["tool_hashes"][name] = sha(path)
        for name in ["objcopy", "objdump", "nm", "size"]:report["tool_hashes"][name] = sha(prefix + name)
        source = out / "probe.cpp"
        source.write_text('#include <stdint.h>\nextern "C" {\nvolatile uint32_t initial=0x12345678, counter;\nvoid probe_main(){for(;;)counter=initial;}\n}\n')
        for target in config["targets"]:
            for mode in target["modes"]:
                for count in [0, 1, 2, 4]:
                    p = regions.plan(target["part"], mode, count)
                    for lto in [False, True]:
                        folder = out / target["part"] / mode / f"units{count}-lto{int(lto)}"; folder.mkdir(parents=True)
                        (folder / "partition.json").write_text(json.dumps(p, indent=2) + "\n")
                        (folder / "probe.ld").write_text(regions.linker(p))
                        flags = [f"-march={target['march']}",f"-mabi={target['abi']}","-Os","-ffunction-sections","-fdata-sections", "-msmall-data-limit=0", "-fno-exceptions", "-fno-rtti", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables"]
                        if lto:flags.append("-flto")
                        obj = folder / "probe.o"; run([compiler,*flags,"-c",source,"-o",obj])
                        def link(extra=(), expect=True, label="image"):
                            return run([compiler,*flags,"-nostdlib","-Wl,--gc-sections","-Wl,--no-warn-rwx-segments",f"-T{folder/'probe.ld'}",f"-Wl,-Map,{folder/(label+'.map')}",obj,*extra,"-o",folder/(label+".elf")],expect)
                        link()
                        run([prefix+"objcopy","-O","binary",folder/"image.elf",folder/"image.bin"])
                        run([prefix+"objcopy","-O","ihex",folder/"image.elf",folder/"image.hex"])
                        nm = run([prefix+"nm","-n",folder/"image.elf"]).stdout;(folder/"symbols.txt").write_text(nm)
                        (folder/"sections.txt").write_text(run([prefix+"size","-A",folder/"image.elf"]).stdout)
                        (folder/"disasm.txt").write_text(run([prefix+"objdump","-d",folder/"image.elf"]).stdout)
                        row = inspect((folder/"image.elf").read_bytes(),(folder/"image.bin").read_bytes(),elf.symbols(nm),p)
                        inspect_hex((folder/"image.hex").read_text(),(folder/"image.bin").read_bytes(),p["link_origin"]+p["app_offset"])
                        row.update(part=target["part"],mode=mode,erase_units=count,lto=lto,image_sha256=sha(folder/"image.bin"),elf_sha256=sha(folder/"image.elf"),hex_sha256=sha(folder/"image.hex"))
                        report["results"].append(row);save()
                        for label,section,size,message in [("reject-app-overflow",".app_fill",p["app_bytes"],"APP"),("reject-persistent-data",".persistent.bad",4,"persistent reservation accepts no input data")]:
                            extra_source=folder/(label+".cpp");extra_obj=folder/(label+".o")
                            extra_source.write_text(f'extern "C" {{ extern const unsigned char extra[{size}]; __attribute__((used,section("{section}"))) const unsigned char extra[{size}]={{1}}; }}\n')
                            run([compiler,*flags,"-c",extra_source,"-o",extra_obj])
                            failed=link([extra_obj],False,label)
                            assert message in failed.stderr,failed.stderr
                            report["negative_checks"].append(dict(part=target["part"],mode=mode,erase_units=count,lto=lto,case=label,rejected=True));save()
        report["status"]="passed"
    except Exception as error:
        report.update(status="failed",error=str(error))
    finally:
        save(); (out/"sha256.json").write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob("*")) if p.is_file() and p.name!="sha256.json"},indent=2)+"\n")
    print(json.dumps(dict(status=report["status"],builds=len(report["results"]),negative_checks=len(report["negative_checks"]),error=report.get("error"))))
    return int(report["status"]!="passed")


if __name__ == "__main__":sys.exit(main())
