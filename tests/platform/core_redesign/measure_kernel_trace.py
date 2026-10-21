#!/usr/bin/env python3
"""N04 canonical-trace controller fixture with fixed WCH X035 port; compile/link only."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import traceback

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
LOCK=REPO/'planning/core-redesign/kernel-trace-inputs.json'
spec=importlib.util.spec_from_file_location('stack_elf',HERE/'runtime/inspect_boot.py')
elf=importlib.util.module_from_spec(spec);spec.loader.exec_module(elf)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--source-dir',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3]
    config=json.loads(LOCK.read_text());commands=[]
    report=dict(status='running',scope=__doc__,source_commit=config['commit'],inputs={},sources=[],results=[])
    temporary=out/'tmp';temporary.mkdir()
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    def run(command,expected_failure=False):
        row=dict(command=[str(x) for x in command]);commands.append(row);save()
        r=subprocess.run(row['command'],cwd=out,capture_output=True,text=True,env={**os.environ,'TMPDIR':str(temporary)})
        row.update(returncode=r.returncode,stdout=r.stdout,stderr=r.stderr);save()
        if expected_failure:
            if r.returncode==0:raise RuntimeError('Expected RV32E register rejection')
        elif r.returncode:raise RuntimeError(str(row))
        return r.stdout
    save()
    try:
        for row in config['sources']:
            p=args.source_dir/row['path'];assert p.stat().st_size==row['bytes'] and sha(p)==row['sha256'],p
            data=p.read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==row['git_blob']
            target=out/'source'/row['path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
            report['sources'].append(row)
        for p in [Path(__file__).resolve(),LOCK,REPO/'planning/core-redesign/runtime-inputs.json',HERE/'runtime/inspect_boot.py',*[p for p in sorted((HERE/'kernel_trace').rglob('*')) if p.suffix in ['.c','.cpp','.h']],REPO/'planning/core-redesign/kernel-trace-traces.json',HERE/'stack_port/support.c']:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
            report['inputs'][str(p.relative_to(REPO))]=sha(p)
        report['compiler']=run([compiler,'--version']).splitlines()[0];report['compiler_sha256']=sha(compiler)
        report['tools']={}
        for name in ['cc1','as','ld','collect2']:
            p=Path(run([compiler,f'-print-prog-name={name}']).strip()).resolve();report['tools'][name]=sha(p)
        for name in ['nm','size','objcopy','objdump']:report['tools'][name]=sha(Path(prefix+name))
        vendor=out/'source/EVT/EXAM/FreeRTOS/FreeRTOS_Core';kernel=vendor/'FreeRTOS';port=kernel/'portable/GCC/RISC-V'
        includes=[HERE/'kernel_trace/target',HERE/'kernel_trace',kernel/'include',port,port/'chip_specific_extensions/RV32I_PFIC_no_extensions',
                  vendor/'Core',vendor/'User',out/'source/EVT/EXAM/SRC/Peripheral/inc',out/'source/EVT/EXAM/SRC/Debug']
        include_flags=[f'-I{p}' for p in includes]
        sources=[HERE/'kernel_trace/target/entry.c',HERE/'kernel_trace/fixture.c',HERE/'stack_port/support.c',kernel/'tasks.c',kernel/'queue.c',kernel/'list.c',port/'port.c',port/'portASM.S']
        profile=next(p for p in json.loads((REPO/'planning/core-redesign/runtime-inputs.json').read_text())['profiles'] if p['x035'])
        for lto in [False,True]:
            group=out/f'lto{int(lto)}';group.mkdir()
            flags=['-march=rv32imac_zicsr_zifencei','-mabi=ilp32','-Os','-ffreestanding','-fno-builtin',
                   '-fno-tree-loop-distribute-patterns','-ffunction-sections','-fdata-sections','-fstack-usage','-msmall-data-limit=0']
            if lto:flags.append('-flto')
            library=Path(run([compiler,*flags,'-print-libgcc-file-name']).strip()).resolve();report['libgcc']=dict(path=str(library),sha256=sha(library))
            cases=[('empty',0,2,64,0)]
            for preempt in config['preemption']:
                for tasks in config['application_tasks']:
                    for words in config['stack_words']:
                        for used in [1,0]:
                            cases.append((('unused-' if not used else '')+f'p{preempt}-tasks{tasks}-w{words}',used,tasks,words,preempt))
            for name,used,tasks,words,preempt in cases:
                dest=group/name;dest.mkdir()
                (dest/'image.ld').write_text(f'''OUTPUT_ARCH(riscv)
ENTRY(probe_main)
MEMORY {{ FLASH (rx) : ORIGIN = 0, LENGTH = {profile['flash_bytes']}
RAM (rwx) : ORIGIN = 0x20000000, LENGTH = {profile['ram_bytes']} }}
SECTIONS {{
.text : {{ KEEP(*(.text.entry)) KEEP(*(.fixture_roots)) *(.text .text.* .rodata .rodata.* .srodata .srodata.*) . = ALIGN(8); }} > FLASH
.init_array : {{ KEEP(*(.init_array .init_array.* .preinit_array .preinit_array.* .ctors)) }} > FLASH
.data : {{ *(.sdata .sdata.* .data .data.*) . = ALIGN(8); }} > RAM AT>FLASH
.bss (NOLOAD) : {{ *(.sbss .sbss.* .bss .bss.*) *(COMMON) }} > RAM
__static_end = .;
.irq_stack ORIGIN(RAM)+LENGTH(RAM)-{config['boot_irq_reservation_bytes']} (NOLOAD) : {{
__boot_irq_start = .; . += {config['boot_irq_reservation_bytes']}; __freertos_irq_stack_top = .;
}} > RAM
__image_end = LOADADDR(.data)+SIZEOF(.data);
ASSERT(__static_end <= __boot_irq_start,"static and boot/IRQ stack overlap")
ASSERT(SIZEOF(.init_array)==0,"constructor arrays not allowed")
/DISCARD/ : {{ *(.comment .eh_frame .eh_frame.* .riscv.attributes) }}
}}
''')
                defines=['-DEXP_HOST=0',f'-DEXP_USED={used}',f'-DEXP_TASKS={tasks}',f'-DEXP_STACK_WORDS={words}',f'-DEXP_PREEMPT={preempt}']
                objects=[]
                for i,source in enumerate(sources):
                    obj=dest/f'{i}-{source.name}.o';objects.append(obj)
                    run([compiler,*flags,*include_flags,*defines,'-c',source,'-o',obj])
                run([compiler,*flags,'-nostdlib','-Wl,--gc-sections','-Wl,--no-warn-rwx-segments',f"-T{dest/'image.ld'}",f"-Wl,-Map,{dest/'image.map'}",*objects,'-lgcc','-o',dest/'image.elf'])
                run([prefix+'objcopy','-O','binary',dest/'image.elf',dest/'image.bin'])
                nm=run([prefix+'nm','-n',dest/'image.elf']);(dest/'symbols.txt').write_text(nm)
                sized=run([prefix+'nm','-S','--size-sort',dest/'image.elf']);(dest/'symbol-sizes.txt').write_text(sized)
                sizes={l.split()[3]:int(l.split()[1],16) for l in sized.splitlines() if len(l.split())==4}
                assert not any(n in sizes for n in ['malloc','calloc','realloc','free','pvPortMalloc','vPortFree','ucHeap'])
                (dest/'sections.txt').write_text(run([prefix+'size','-A',dest/'image.elf']))
                (dest/'disasm.txt').write_text(run([prefix+'objdump','-d',dest/'image.elf']))
                _,sections=elf.elf_sections((dest/'image.elf').read_bytes());symbols=elf.symbols(nm)
                assert symbols['probe_main']==0 and (dest/'image.bin').stat().st_size==symbols['__image_end']
                data=sections.get('.data',{}).get('size',0);bss=sections.get('.bss',{}).get('size',0)
                reserve=sections['.irq_stack']['size'];assert reserve==config['boot_irq_reservation_bytes']
                if used:assert reserve>config['port_mscratch_offset_bytes']
                stacks=sum(value for key,value in sizes.items() if key.startswith(('application_stack','idle_stack','driver_stack')))
                tcbs=sum(value for key,value in sizes.items() if key.startswith(('application_control','idle_control','driver_control')))
                queues=sum(value for key,value in sizes.items() if key.startswith(('queue_control','queue_storage','queues')))
                if used:
                    assert stacks==(tasks+2)*words*4
                    # Small fixture wrappers may be inlined by LTO. The four
                    # actual port entries must remain addressable.
                    assert all(n in symbols for n in ['SW_Handler','SysTick_Handler','pxPortInitialiseStack','xPortStartFirstTask'])
                else:
                    baseline=next(r for r in report['results'] if r['lto']==lto and r['case']=='empty') if name!='empty' else None
                    assert data+bss==stacks==tcbs==queues==0
                    if baseline:assert sha(dest/'image.bin')==baseline['image_sha256']
                frames=[]
                for path in dest.glob('*.su'):
                    for line in path.read_text().splitlines():
                        parts=line.split('\t')
                        if len(parts)==3:frames.append(int(parts[1]))
                report['results'].append(dict(part=profile['part'],lto=lto,case=name,used=bool(used),application_tasks=tasks,preemption=preempt,
                    words_per_stack=words,image_bytes=(dest/'image.bin').stat().st_size,data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,
                    stack_arrays_bytes=stacks,driver_stack_bytes=sizes.get('driver_stack',0),driver_control_bytes=sizes.get('driver_control',0),tcb_objects_bytes=tcbs,queue_objects_bytes=queues,boot_irq_reserved_bytes=reserve,
                    port_mscratch_offset_bytes=config['port_mscratch_offset_bytes'] if used else None,
                    c_irq_space_below_initial_sp_bytes=reserve-config['port_mscratch_offset_bytes'] if used else None,
                    ram_with_reservation_bytes=data+bss+reserve,max_reported_frame_bytes=max(frames,default=0),stack_peak_bytes=None,image_sha256=sha(dest/'image.bin'),elf_sha256=sha(dest/'image.elf')))
                save()
        # This fixed vendor assembly saves the full I register set. Do not
        # advertise it as a V003/E port merely because C types are 32-bit.
        run([compiler,'-march=rv32ec_zicsr','-mabi=ilp32e',*include_flags,'-c',port/'portASM.S','-o',out/'rv32e-negative.o'],expected_failure=True)
        report['rv32e']='assembler rejected full-register port as expected'
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error),traceback=traceback.format_exc())
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],builds=len(report['results']),error=report.get('error'))))
    return int(report['status']!='passed')


if __name__=='__main__':sys.exit(main())
