#!/usr/bin/env python3
"""EXP-04 sync storage versus leased scan/prepared-write integration ABI; no MCU execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[2]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);loaded=importlib.util.module_from_spec(spec);spec.loader.exec_module(loaded);return loaded
regions=module("scan_regions",ROOT/"flash_regions.py")
elf=module("scan_elf",ROOT/"runtime/inspect_boot.py")
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler",type=Path,required=True);parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3];config=json.loads(regions.MANIFEST.read_text())
    report=dict(status="running",scope=__doc__,inputs={},results=[],quantum=32,backend_lto=False);commands=[]
    def save():
        (out/"result.json").write_text(json.dumps(report,indent=2)+"\n");(out/"commands.json").write_text(json.dumps(commands,indent=2)+"\n")
    def run(command):
        item=dict(command=[str(p) for p in command],returncode=None);commands.append(item);save()
        p=subprocess.run(item["command"],capture_output=True,text=True,cwd=out);item.update(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr);save()
        if p.returncode:raise RuntimeError(str(item))
        return p.stdout
    save()
    try:
        for path in [Path(__file__).resolve(),ROOT/"flash_integrated_entry.cpp",ROOT/"flash_store_backend_probe.cpp",ROOT/"flash_regions.py",ROOT/"runtime/inspect_boot.py",regions.MANIFEST,REPO/"planning/core-redesign/flash-format-inputs.json",REPO/"tests/unit/core_redesign/flash_scan.hpp",REPO/"tests/unit/core_redesign/flash_integrated.hpp",REPO/"tests/unit/core_redesign/flash_store.hpp"]:
            relative=path.relative_to(REPO);dest=out/"inputs"/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest);report["inputs"][str(relative)]=sha(path)
        report["compiler"]=run([compiler,"--version"]).splitlines()[0];report["compiler_sha256"]=sha(compiler);report["tools"]={}
        for name in ["cc1plus","as","ld","collect2"]:
            path=Path(run([compiler,f"-print-prog-name={name}"]).strip()).resolve();report["tools"][name]=sha(path)
        for name in ["nm","size","objcopy","objdump"]:report["tools"][name]=sha(prefix+name)
        for profile in config["targets"]:
            examples=[("raw","fast",1,1),("blob","fast",1,2),("blob-two","fast",2,3),("keys-two","fast",2,5),("blob-commit","fast",4,6),("keys-commit","fast",4,7),("keys-large","fast-erase-1k",4,5)]
            if "standard" in profile["modes"]:examples += [("keys-standard","standard",2,5),("keys-standard-commit","standard",2,7)]
            cases=[("empty","fast",0,0,0),("unused","fast",0,7,3),("table-only","fast",0,0,4)]
            cases += [(label+suffix,mode,units,case,style) for label,mode,units,case in examples for suffix,style in [("-sync",1),("-integrated",2)]]
            for lto in [False,True]:
                group=out/profile["part"]/f"lto{int(lto)}";group.mkdir(parents=True)
                flags=[f"-march={profile['march']}",f"-mabi={profile['abi']}","-Os","-std=c++17","-ffreestanding","-fno-builtin","-fno-tree-loop-distribute-patterns","-ffunction-sections","-fdata-sections","-fno-exceptions","-fno-rtti","-fno-threadsafe-statics","-fno-unwind-tables","-fno-asynchronous-unwind-tables","-fstack-usage"]
                backend=group/"backend.o";run([compiler,*flags,"-fno-lto","-c",ROOT/"flash_store_backend_probe.cpp","-o",backend])
                if lto:flags.append("-flto")
                for label,mode,units,case,style in cases:
                    dest=group/label;dest.mkdir();g=profile["modes"][mode];partition=regions.plan(profile["part"],mode,units)
                    (dest/"partition.json").write_text(json.dumps(partition,indent=2)+"\n");(dest/"image.ld").write_text(regions.linker(partition))
                    logical_units=units or 4
                    origin=profile["physical_origin"]+profile["flash_bytes"]-logical_units*g["erase_unit"]
                    defines=[f"-DCASE={case}",f"-DSTYLE={style}",f"-DPROGRAM={g['program_unit']}",f"-DERASE={g['erase_unit']}",f"-DUNITS={logical_units}",f"-DREGION_ORIGIN={origin}",f"-DFLASH_ORIGIN={profile['physical_origin']}",f"-DFLASH_BYTES={profile['flash_bytes']}"]
                    obj=dest/"entry.o";run([compiler,*flags,*defines,"-c",ROOT/"flash_integrated_entry.cpp","-o",obj])
                    run([compiler,*flags,"-nostdlib","-Wl,--gc-sections","-Wl,--no-warn-rwx-segments",f"-T{dest/'image.ld'}",f"-Wl,-Map,{dest/'image.map'}",obj,backend,"-o",dest/"image.elf"])
                    run([prefix+"objcopy","-O","binary",dest/"image.elf",dest/"image.bin"])
                    nm=run([prefix+"nm","-n",dest/"image.elf"]);(dest/"symbols.txt").write_text(nm)
                    sizes_text=run([prefix+"nm","-S","--size-sort",dest/"image.elf"]);(dest/"symbol-sizes.txt").write_text(sizes_text)
                    sizes={line.split()[3]:int(line.split()[1],16) for line in sizes_text.splitlines() if len(line.split())==4}
                    assert not any(name in sizes for name in ["malloc","calloc","realloc","free","_Znwj","_Znaj"])
                    (dest/"disasm.txt").write_text(run([prefix+"objdump","-d",dest/"image.elf"]))
                    (dest/"sections.txt").write_text(run([prefix+"size","-A",dest/"image.elf"]))
                    raw=(dest/"image.elf").read_bytes();h,sections=elf.elf_sections(raw);names=elf.symbols(nm)
                    assert h[4]==names["probe_main"]
                    assert not any(sections.get(n,{}).get("size",0) for n in [".init_array",".preinit_array",".ctors"])
                    data=sections.get(".data",{}).get("size",0);bss=sections.get(".bss",{}).get("size",0);image=(dest/"image.bin").stat().st_size
                    assert image<=partition["app_bytes"] and data+bss<=profile["ram_bytes"]
                    frames=[]
                    for path in [*dest.glob("*.su"),group/"backend.su"]:
                        if path.exists():
                            for line in path.read_text().splitlines():
                                fields=line.split("\t")
                                if len(fields)==3:frames.append(int(fields[1]))
                    row=dict(part=profile["part"],lto=lto,case=label,style=style,mode=mode,erase_units=units,image_bytes=image,data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,
                             integrated_object_bytes=sizes.get("integrated_probe",0),registry_object_bytes=sizes.get("registry_probe",0),sync_object_bytes=sizes.get("sync_probe",0),
                             harness_object_bytes=sum(sizes.get(n,0) for n in ["operation","argument","observed","owner_tag","table_ticket","payload","answer"]),backend_object_bytes=sum(sizes.get(n,0) for n in ["backend_accept","backend_offset","backend_observed"]),reserved_bytes=partition["reserved_bytes"],app_capacity_bytes=partition["app_bytes"],max_emitted_frame_bytes=max(frames,default=0),image_sha256=sha(dest/"image.bin"),elf_sha256=sha(dest/"image.elf"))
                    if style==3:
                        baseline=next(r for r in report["results"] if r["part"]==profile["part"] and r["lto"]==lto and r["case"]=="empty")
                        assert row["static_ram_bytes"]==row["integrated_object_bytes"]==row["registry_object_bytes"]==0 and row["image_sha256"]==baseline["image_sha256"]
                    report["results"].append(row);save()
        report["status"]="passed"
    except Exception as error:report.update(status="failed",error=str(error))
    finally:
        save();(out/"sha256.json").write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob("*")) if p.is_file() and p.name!="sha256.json"},indent=2)+"\n")
    print(json.dumps(dict(status=report["status"],builds=len(report["results"]),error=report.get("error"))))
    return int(report["status"]!="passed")


if __name__=="__main__":sys.exit(main())
