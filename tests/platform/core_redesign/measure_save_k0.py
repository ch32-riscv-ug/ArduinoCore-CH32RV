#!/usr/bin/env python3
"""EXP-06 N05 conditional K0 slave plus mounted blob storage; opaque synchronous media backend, no physical Flash/IRQ or total stack fit proof."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parent;REPO=ROOT.parents[2]
INPUT=REPO/'planning/core-redesign/save-k0-inputs.json'
PROFILES=REPO/'planning/core-redesign/runtime-inputs.json'
UNIT=REPO/'tests/unit/core_redesign'
spec=importlib.util.spec_from_file_location('display_elf',ROOT/'runtime/inspect_boot.py');elf=importlib.util.module_from_spec(spec);spec.loader.exec_module(elf)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3];profiles=json.loads(PROFILES.read_text());config=json.loads(INPUT.read_text());temporary=out/'tmp';temporary.mkdir()
    report=dict(status='running',scope=__doc__,inputs={},results=[],excluded=[],libgcc={},support_lto=False);commands=[]
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');None
    def run(command):
        item=dict(command=[str(x) for x in command],returncode=None);commands.append(item)
        with (out/'commands.jsonl').open('a') as log:log.write(json.dumps(dict(event='start',**item))+'\n')
        r=subprocess.run(item['command'],cwd=out,capture_output=True,text=True,env={**os.environ,'TMPDIR':str(temporary)});item.update(returncode=r.returncode,stdout=r.stdout,stderr=r.stderr)
        with (out/'commands.jsonl').open('a') as log:log.write(json.dumps(dict(event='finish',**item))+'\n')
        if r.returncode:raise RuntimeError(str(item))
        return r.stdout
    save()
    try:
        for path in [Path(__file__).resolve(),INPUT,PROFILES,*[UNIT/name for name in ['save_coexistence.hpp','flash_integrated.hpp','flash_scan.hpp','flash_store.hpp','k0_slave_register.hpp','k0_shared.hpp','k0_candidates.hpp','data_parts.hpp','work_executor.hpp','event_executor.hpp']],ROOT/'save_k0_entry.cpp',ROOT/'irq_gate.hpp',ROOT/'flash_store_backend_probe.cpp',ROOT/'runtime/inspect_boot.py']:
            relative=path.relative_to(REPO);dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest);report['inputs'][str(relative)]=sha(path)
        report['compiler']=run([compiler,'--version']).splitlines()[0];report['compiler_sha256']=sha(compiler);report['tools']={}
        for name in ['cc1plus','as','ld','collect2']:
            p=Path(run([compiler,f'-print-prog-name={name}']).strip()).resolve();report['tools'][name]=sha(p)
        for name in ['nm','size','objcopy','objdump']:report['tools'][name]=sha(prefix+name)
        for source_profile in [p for p in profiles['profiles'] if not p['x035']]:
            profile={**source_profile,'march':source_profile['march']+'_zifencei'}
            cases=[('empty',0,1,0,0,0)]
            for family,name in enumerate(config['candidates'],1):
                for admission in [0,1]:
                    for units in config['units']:
                        cases.append((f'{name}-p{admission}-u{units}',1,family,0,units,admission))
                    cases.append((f'unused-{name}-p{admission}',1,family,1,4,admission))
            for lto in [False,True]:
                group=out/profile['part']/f'lto{int(lto)}';group.mkdir(parents=True)
                flags=[f"-march={profile['march']}",f"-mabi={profile['abi']}",'-Os','-std=c++17','-ffreestanding','-fno-builtin','-fno-tree-loop-distribute-patterns','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage']
                if lto:flags.append('-flto')
                library=Path(run([compiler,*flags,'-print-libgcc-file-name']).strip()).resolve();report['libgcc'][profile['part']]=dict(path=str(library),sha256=sha(library))
                for label,case,family,unused,units,admission in cases:
                    dest=group/label;dest.mkdir()
                    reserved=units*64 if case and not unused else 0
                    (dest/'image.ld').write_text(f'''OUTPUT_ARCH(riscv)
ENTRY(probe_main)
MEMORY {{ FLASH (rx) : ORIGIN = 0, LENGTH = {profile['flash_bytes']-reserved}
STORE (rw) : ORIGIN = {profile['flash_bytes']-reserved}, LENGTH = {reserved}
RAM (rwx) : ORIGIN = 0x20000000, LENGTH = {profile['ram_bytes']} }}
SECTIONS {{
.text : {{ KEEP(*(.text.entry)) *(.text .text.* .rodata .rodata.* .srodata .srodata.*) . = ALIGN(8); }} > FLASH
.init_array : {{ KEEP(*(.init_array .init_array.* .preinit_array .preinit_array.* .ctors)) }} > FLASH
.forbidden_persistent : {{ KEEP(*(.persistent .persistent.*)) }} > FLASH
.data : {{ *(.sdata .sdata.* .data .data.*) . = ALIGN(8); }} > RAM AT> FLASH
.bss (NOLOAD) : {{ *(.sbss .sbss.* .bss .bss.*) *(COMMON) }} > RAM
__image_end = LOADADDR(.data) + SIZEOF(.data);
.persistent (NOLOAD) : {{ __persistent_start = .; . += {reserved}; __persistent_end = .; }} > STORE
ASSERT(__image_end <= {profile['flash_bytes']-reserved},"application overlaps persistent reservation")
ASSERT(SIZEOF(.forbidden_persistent)==0,"persistent reservation accepts no input data")
ASSERT(SIZEOF(.init_array)==0,"constructor arrays not allowed in probe")
/DISCARD/ : {{ *(.comment .eh_frame .eh_frame.* .riscv.attributes) }}
}}
''')
                    defines=[f'-DCASE={case}',f'-DUNUSED={unused}',f'-DFAMILY={family}',f'-DUNITS={units}','-DX035=0','-DCAPACITY=1',f'-DADMISSION={admission}'];objects=[]
                    for source in ['save_k0_entry.cpp','flash_store_backend_probe.cpp']:
                        obj=dest/(source+'.o');extra=['-fno-lto'] if source=='flash_store_backend_probe.cpp' else []
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
                    assert len(image)<=profile['flash_bytes']-reserved and data+bss<=profile['ram_bytes']
                    assert sections.get('.persistent',{}).get('size',0)==reserved
                    if reserved:assert sections['.persistent']['type']==8 and sections['.persistent']['address']==profile['flash_bytes']-reserved
                    frames=[]
                    for path in dest.glob('*.su'):
                        for line in path.read_text().splitlines():
                            f=line.split('\t')
                            if len(f)==3:frames.append(int(f[1]))
                    row=dict(part=profile['part'],lto=lto,case=label,unused=bool(unused),family=family,admission=admission,erase_units=units,reserved_bytes=reserved,app_capacity_bytes=profile['flash_bytes']-reserved,memory_scope='V003 conditional slave capacity1; two admission policies; opaque synchronous storage boundary; no physical Flash/IRQ execution',image_bytes=len(image),data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,application_object_bytes=sizes.get('application_probe',0),saver_object_bytes=sizes.get('saver_probe',0),backend_object_bytes=sum(sizes.get(n,0) for n in ['backend_accept','backend_offset','backend_observed']),harness_object_bytes=sum(sizes.get(n,0) for n in ['operation','argument','observed']),max_emitted_frame_bytes=max(frames,default=0),image_sha256=sha(dest/'image.bin'),elf_sha256=sha(dest/'image.elf'))
                    if unused:
                        baseline=next(v for v in report['results'] if v['part']==profile['part'] and v['lto']==lto and v['case']=='empty')
                        assert row['static_ram_bytes']==row['application_object_bytes']==row['saver_object_bytes']==row['backend_object_bytes']==0 and row['image_sha256']==baseline['image_sha256']
                    report['results'].append(row);save()
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error))
    finally:
        save();(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],builds=len(report['results']),error=report.get('error'))));return int(report['status']!='passed')


if __name__=='__main__':sys.exit(main())
