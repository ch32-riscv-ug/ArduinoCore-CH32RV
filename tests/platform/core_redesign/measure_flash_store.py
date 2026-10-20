#!/usr/bin/env python3
"""EXP-04 C++ ABI cost with opaque quiescent backend stubs; not Flash firmware."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parent
REPO=ROOT.parents[2]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    loaded=importlib.util.module_from_spec(spec);spec.loader.exec_module(loaded)
    return loaded
regions=module("store_regions",ROOT/"flash_regions.py")
elf=module("store_elf",ROOT/"runtime/inspect_boot.py")
INPUT=REPO/"planning/core-redesign/flash-format-inputs.json"
HEADER=REPO/"tests/unit/core_redesign/flash_store.hpp"
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler",type=Path,required=True);parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3]
    config=json.loads(INPUT.read_text());profiles=json.loads(regions.MANIFEST.read_text())
    report=dict(status="running",scope=__doc__,inputs={},results=[],unsupported=[],backend_lto=False)
    commands=[]
    def save():
        (out/"result.json").write_text(json.dumps(report,indent=2)+"\n")
        (out/"commands.json").write_text(json.dumps(commands,indent=2)+"\n")
    def run(command):
        entry=dict(command=[str(p) for p in command],returncode=None);commands.append(entry);save()
        result=subprocess.run(entry["command"],cwd=out,capture_output=True,text=True)
        entry.update(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr);save()
        if result.returncode:raise RuntimeError(str(entry))
        return result.stdout
    save()
    try:
        for path in [Path(__file__).resolve(),HEADER,INPUT,regions.MANIFEST,ROOT/"flash_regions.py",ROOT/"runtime/inspect_boot.py",ROOT/"flash_store_entry.cpp",ROOT/"flash_store_backend_probe.cpp"]:
            relative=path.relative_to(REPO);dest=out/"inputs"/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest);report["inputs"][str(relative)]=sha(path)
        report["compiler"]=run([compiler,"--version"]).splitlines()[0];report["compiler_sha256"]=sha(compiler)
        report["tools"]={}
        for name in ["cc1plus","as","ld","collect2"]:
            tool=Path(run([compiler,f"-print-prog-name={name}"]).strip()).resolve();report["tools"][name]=sha(tool)
        for name in ["nm","size","objcopy","objdump"]:report["tools"][name]=sha(prefix+name)
        for profile in profiles["targets"]:
            cases=[("empty","fast",0,0,True)]
            for mode,g in profile["modes"].items():
                for units in config["erase_units"]:
                    for i,candidate in enumerate(config["candidates"]):
                        bank=(units//(2 if candidate["policy"]=="two-bank" else 1))*g["erase_unit"]
                        footer=((12+g["program_unit"]-1)//g["program_unit"])*g["program_unit"] if candidate["commit"] else 0
                        payload=12 if candidate["kind"]=="keys" else 8;header=0 if candidate["kind"]=="raw" else 16
                        body=((header+payload+g["program_unit"]-1)//g["program_unit"])*g["program_unit"]
                        fits=(candidate["policy"]!="two-bank" or units%2==0) and bank>=body+footer
                        label=f"{mode}-units{units}-{candidate['id']}"
                        if fits:cases.append((label,mode,units,i+1,False))
                        else:report["unsupported"].append(dict(part=profile["part"],mode=mode,erase_units=units,candidate=candidate["id"]))
            cases += [("unused-"+c["id"],"fast",0,i+1,True) for i,c in enumerate(config["candidates"])]
            cases.append(("unused-reserved","fast",4,7,True))
            for lto in [False,True]:
                group=out/profile["part"]/f"lto{int(lto)}";group.mkdir(parents=True)
                flags=[f"-march={profile['march']}",f"-mabi={profile['abi']}","-Os","-std=c++17","-ffreestanding","-fno-builtin","-fno-tree-loop-distribute-patterns","-ffunction-sections","-fdata-sections","-fno-exceptions","-fno-rtti","-fno-threadsafe-statics","-fno-unwind-tables","-fno-asynchronous-unwind-tables","-fstack-usage"]
                backend=group/"backend.o";run([compiler,*flags,"-fno-lto","-c",ROOT/"flash_store_backend_probe.cpp","-o",backend])
                if lto:flags.append("-flto")
                for label,mode,units,case,unused in cases:
                    dest=group/label;dest.mkdir();g=profile["modes"][mode]
                    partition=regions.plan(profile["part"],mode,units)
                    (dest/"partition.json").write_text(json.dumps(partition,indent=2)+"\n")
                    (dest/"image.ld").write_text(regions.linker(partition))
                    defines=[f"-DCASE={case}",f"-DUNUSED={int(unused)}",f"-DPROGRAM={g['program_unit']}",f"-DERASE={g['erase_unit']}",f"-DUNITS={4 if unused else units}"]
                    obj=dest/"entry.o";run([compiler,*flags,*defines,"-c",ROOT/"flash_store_entry.cpp","-o",obj])
                    run([compiler,*flags,"-nostdlib","-Wl,--gc-sections","-Wl,--no-warn-rwx-segments",f"-T{dest/'image.ld'}",f"-Wl,-Map,{dest/'image.map'}",obj,backend,"-o",dest/"image.elf"])
                    run([prefix+"objcopy","-O","binary",dest/"image.elf",dest/"image.bin"])
                    nm=run([prefix+"nm","-n",dest/"image.elf"]);(dest/"symbols.txt").write_text(nm)
                    sizes_text=run([prefix+"nm","-S","--size-sort",dest/"image.elf"]);(dest/"symbol-sizes.txt").write_text(sizes_text)
                    sizes={line.split()[3]:int(line.split()[1],16) for line in sizes_text.splitlines() if len(line.split())==4}
                    assert not any(name in sizes for name in ["malloc","calloc","realloc","free","_Znwj","_Znaj"])
                    (dest/"disasm.txt").write_text(run([prefix+"objdump","-d",dest/"image.elf"]))
                    (dest/"sections.txt").write_text(run([prefix+"size","-A",dest/"image.elf"]))
                    raw=(dest/"image.elf").read_bytes();header,sections=elf.elf_sections(raw)
                    assert header[4]==elf.symbols(nm)["probe_main"]
                    assert not any(sections.get(n,{}).get("size",0) for n in [".init_array",".preinit_array",".ctors"])
                    data=sections.get(".data",{}).get("size",0);bss=sections.get(".bss",{}).get("size",0)
                    image_size=(dest/"image.bin").stat().st_size
                    assert image_size<=partition["app_bytes"] and data+bss<=profile["ram_bytes"]
                    frames=[]
                    for su in [*dest.glob("*.su"),group/"backend.su"]:
                        if not su.exists():continue
                        for line in su.read_text().splitlines():
                            fields=line.split("\t")
                            if len(fields)==3:frames.append(int(fields[1]))
                    row=dict(part=profile["part"],lto=lto,case=label,mode=mode,erase_units=units,unused=unused,
                             image_bytes=image_size,data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,
                             store_object_bytes=sizes.get("store_probe",0),harness_object_bytes=sum(sizes.get(n,0) for n in ["operation","argument","observed","input"]),
                             backend_object_bytes=sum(sizes.get(n,0) for n in ["backend_accept","backend_offset","backend_observed"]),
                             reserved_bytes=partition["reserved_bytes"],app_capacity_bytes=partition["app_bytes"],scratch_capacity_bytes=g["program_unit"] if not unused else 0,
                             max_emitted_frame_bytes=max(frames,default=0),image_sha256=sha(dest/"image.bin"),elf_sha256=sha(dest/"image.elf"))
                    if unused:
                        assert row["store_object_bytes"]==row["static_ram_bytes"]==0
                        assert not any(n in sizes for n in ["flash_probe_read","flash_probe_erase","flash_probe_program"])
                        if label!="empty":assert row["image_sha256"]==next(v["image_sha256"] for v in report["results"] if v["part"]==profile["part"] and v["lto"]==lto and v["case"]=="empty")
                    report["results"].append(row);save()
        report["status"]="passed"
    except Exception as error:report.update(status="failed",error=str(error))
    finally:
        save();(out/"sha256.json").write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob("*")) if p.is_file() and p.name!="sha256.json"},indent=2)+"\n")
    print(json.dumps(dict(status=report["status"],builds=len(report["results"]),unsupported=len(report["unsupported"]),error=report.get("error"))))
    return int(report["status"]!="passed")


if __name__=="__main__":sys.exit(main())
