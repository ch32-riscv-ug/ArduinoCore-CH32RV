#!/usr/bin/env python3
"""EXP-01 I2C split completion loans with resource ownership ABI; logical clock/pin leases only, no startup or IRQ backend."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[2]
INPUT=REPO/'planning/core-redesign/i2c-managed-handoff-inputs.json'
PROFILES=REPO/'planning/core-redesign/runtime-inputs.json'
UNIT=REPO/'tests/unit/core_redesign'
spec=importlib.util.spec_from_file_location('i2c_resource_elf',ROOT/'runtime/inspect_boot.py');elf=importlib.util.module_from_spec(spec);spec.loader.exec_module(elf)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3];profiles=json.loads(PROFILES.read_text());config=json.loads(INPUT.read_text())
    report=dict(status='running',scope=__doc__,inputs={},results=[],excluded=[],libgcc={},support_lto=False);commands=[]
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    def run(command):
        item=dict(command=[str(x) for x in command],returncode=None);commands.append(item);save()
        r=subprocess.run(item['command'],cwd=out,capture_output=True,text=True);item.update(returncode=r.returncode,stdout=r.stdout,stderr=r.stderr);save()
        if r.returncode:raise RuntimeError(str(item))
        return r.stdout
    save()
    try:
        for path in [Path(__file__).resolve(),INPUT,PROFILES,UNIT/'i2c_managed_handoff.hpp',UNIT/'i2c_handoff.hpp',UNIT/'i2c_resources.hpp',ROOT/'runtime/i2c_master.hpp',ROOT/'i2c_managed_handoff_entry.cpp',ROOT/'acquisition_support.cpp',ROOT/'runtime/inspect_boot.py']:
            relative=path.relative_to(REPO);dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest);report['inputs'][str(relative)]=sha(path)
        report['compiler']=run([compiler,'--version']).splitlines()[0];report['compiler_sha256']=sha(compiler);report['tools']={}
        for name in ['cc1plus','as','ld','collect2']:
            p=Path(run([compiler,f'-print-prog-name={name}']).strip()).resolve();report['tools'][name]=sha(p)
        for name in ['nm','size','objcopy','objdump']:report['tools'][name]=sha(prefix+name)
        for profile in profiles['profiles']:
            cases=[('empty',0,0,0),('baseline-dedicated',1,0,0),('dedicated',1,0,1),('baseline-pooled',2,0,0),('pooled',2,0,1),('unused-dedicated',1,1,1),('unused-pooled',2,1,1)]
            for lto in [False,True]:
                group=out/profile['part']/f'lto{int(lto)}';group.mkdir(parents=True)
                flags=[f"-march={profile['march']}",f"-mabi={profile['abi']}",'-Os','-std=c++17','-ffreestanding','-fno-builtin','-fno-tree-loop-distribute-patterns','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage']
                if lto:flags.append('-flto')
                library=Path(run([compiler,*flags,'-print-libgcc-file-name']).strip()).resolve();report['libgcc'][profile['part']]=dict(path=str(library),sha256=sha(library))
                for label,case,unused,managed in cases:
                    dest=group/label;dest.mkdir()
                    (dest/'image.ld').write_text(f'''OUTPUT_ARCH(riscv)
ENTRY(probe_main)
MEMORY {{ FLASH (rx) : ORIGIN = 0, LENGTH = {profile['flash_bytes']}
RAM (rwx) : ORIGIN = 0x20000000, LENGTH = {profile['ram_bytes']} }}
SECTIONS {{
.text : {{ KEEP(*(.text.entry)) *(.text .text.* .rodata .rodata.* .srodata .srodata.*) . = ALIGN(8); }} > FLASH
.init_array : {{ KEEP(*(.init_array .init_array.* .preinit_array .preinit_array.* .ctors)) }} > FLASH
.data : {{ *(.sdata .sdata.* .data .data.*) . = ALIGN(8); }} > RAM AT> FLASH
.bss (NOLOAD) : {{ *(.sbss .sbss.* .bss .bss.*) *(COMMON) }} > RAM
__image_end = LOADADDR(.data) + SIZEOF(.data);
ASSERT(SIZEOF(.init_array)==0,"constructor arrays not allowed in probe")
/DISCARD/ : {{ *(.comment .eh_frame .eh_frame.* .riscv.attributes) }}
}}
''')
                    defines=[f'-DMANAGED={managed}',f'-DCASE={case}',f'-DUNUSED={unused}',f"-DBUFFER_BYTES={config['buffer_bytes']}"];objects=[]
                    for source in ['i2c_managed_handoff_entry.cpp','acquisition_support.cpp']:
                        obj=dest/(source+'.o');extra=['-fno-lto'] if source=='acquisition_support.cpp' else []
                        run([compiler,*flags,*extra,*defines,'-c',ROOT/source,'-o',obj]);objects.append(obj)
                    run([compiler,*flags,'-nostdlib','-Wl,--gc-sections','-Wl,--no-warn-rwx-segments',f"-T{dest/'image.ld'}",f"-Wl,-Map,{dest/'image.map'}",*objects,'-lgcc','-o',dest/'image.elf'])
                    run([prefix+'objcopy','-O','binary',dest/'image.elf',dest/'image.bin'])
                    nm=run([prefix+'nm','-n',dest/'image.elf']);(dest/'symbols.txt').write_text(nm)
                    sizes_text=run([prefix+'nm','-S','--size-sort',dest/'image.elf']);(dest/'symbol-sizes.txt').write_text(sizes_text)
                    sizes={line.split()[3]:int(line.split()[1],16) for line in sizes_text.splitlines() if len(line.split())==4}
                    assert not any(n in sizes for n in ['malloc','calloc','realloc','free','_Znwj','_Znaj'])
                    (dest/'sections.txt').write_text(run([prefix+'size','-A',dest/'image.elf']));(dest/'disasm.txt').write_text(run([prefix+'objdump','-d',dest/'image.elf']))
                    raw=(dest/'image.elf').read_bytes();image=(dest/'image.bin').read_bytes();h,sections=elf.elf_sections(raw);symbols=elf.symbols(nm)
                    assert h[4]==symbols['probe_main']==0 and len(image)==symbols['__image_end']
                    data=sections.get('.data',{}).get('size',0);bss=sections.get('.bss',{}).get('size',0)
                    assert data==0
                    assert len(image)<=profile['flash_bytes'] and data+bss<=profile['ram_bytes']
                    frames=[]
                    for path in dest.glob('*.su'):
                        for line in path.read_text().splitlines():
                            f=line.split('\t')
                            if len(f)==3:frames.append(int(f[1]))
                    row=dict(part=profile['part'],lto=lto,case=label,unused=bool(unused),image_bytes=len(image),data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,manager_object_bytes=sizes.get('manager_probe',0),resource_object_bytes=sizes.get('resources_probe',0),pins_object_bytes=sizes.get('pins_probe',0),buffer_object_bytes=sizes.get('buffers_probe',0),harness_object_bytes=sum(sizes.get(n,0) for n in ['operation','argument','observed','output','completion','byte']),max_emitted_frame_bytes=max(frames,default=0),image_sha256=sha(dest/'image.bin'),elf_sha256=sha(dest/'image.elf'))
                    if unused:
                        baseline=next(v for v in report['results'] if v['part']==profile['part'] and v['lto']==lto and v['case']=='empty')
                        assert row['static_ram_bytes']==row['manager_object_bytes']+row['buffer_object_bytes']+row['resource_object_bytes']+row['pins_object_bytes']==0 and row['image_sha256']==baseline['image_sha256']
                    report['results'].append(row);save()
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],builds=len(report['results']),error=report.get('error'))));return int(report['status']!='passed')


if __name__=='__main__':sys.exit(main())
