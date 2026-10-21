#!/usr/bin/env python3
"""Build the frozen upstream V003 I2C knob twice, without running or uploading it."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import urllib.request

ROOT=Path(__file__).resolve().parents[3]
INPUT=ROOT/'planning/core-redesign/k0-upstream-inputs.json'
spec=importlib.util.spec_from_file_location('upstream_elf',Path(__file__).parent/'runtime/inspect_boot.py')
elf=importlib.util.module_from_spec(spec);spec.loader.exec_module(elf)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    source=parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--source-root',type=Path,help='Root with the locked upstream paths, including LICENSE')
    source.add_argument('--fetch',action='store_true',help='Explicitly download only the fixed URLs into the new output directory')
    parser.add_argument('--compiler',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    config=json.loads(INPUT.read_text());compiler=args.compiler.resolve();prefix=str(compiler)[:-3]
    report=dict(status='running',scope=__doc__,inputs={},sources={},dependencies={},results=[],downloads=[],execution=False)
    commands=[];temporary=out/'tmp';temporary.mkdir();environment=dict(os.environ,TMPDIR=str(temporary))
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    def run(command,cwd=None,stdin=None):
        item=dict(command=[str(v) for v in command],cwd=str(cwd or ROOT),environment=dict(TMPDIR=str(temporary)),returncode=None);commands.append(item);save()
        result=subprocess.run(item['command'],cwd=cwd or ROOT,input=stdin,capture_output=True,text=True,timeout=180,env=environment)
        item.update(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr);save()
        if result.returncode:raise RuntimeError(item)
        return result.stdout
    save()
    try:
        source_root=out/'source'
        if args.fetch:
            for item in config['files']:
                with urllib.request.urlopen(item['url'],timeout=45) as response:data=response.read()
                assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['path']
                target=source_root/item['path'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
                report['downloads'].append(dict(url=item['url'],bytes=len(data),sha256=item['sha256']));save()
        else:
            for item in config['files']:
                original=args.source_root.resolve()/item['path']
                assert original.stat().st_size==item['bytes'] and sha(original)==item['sha256'],item['path']
                target=source_root/item['path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,target)
        for item in config['files']:report['sources'][item['path']]=item['sha256']
        for path in [INPUT,Path(__file__).resolve(),Path(__file__).parent/'runtime/inspect_boot.py']:
            relative=path.relative_to(ROOT);target=out/'inputs'/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target);report['inputs'][str(relative)]=sha(path)
        project=source_root/config['project'];flags=config['original_flags'];sources=config['compile_sources']
        assert sources==sorted(str(p.relative_to(project)) for p in project.glob('src/*.c'))
        assert not list(project.glob('*.c')) and not list(project.glob('src/*.S'))
        report['compiler']=run([compiler,'--version']).splitlines()[0];report['compiler_sha256']=sha(compiler)
        report['tools']={}
        for name in ['cc1','as','ld','collect2']:
            path=Path(run([compiler,f'-print-prog-name={name}']).strip()).resolve();report['tools'][name]=sha(path)
        for name in ['nm','size','objcopy','objdump']:report['tools'][name]=sha(Path(prefix+name))
        library=Path(run([compiler,*flags,'-print-libgcc-file-name'],project).strip()).resolve()
        report['libgcc']=dict(path=str(library),sha256=sha(library))
        for source in sources:
            text=run([compiler,*flags,'-M',source],project)
            name=Path(source).stem;(out/(name+'.d')).write_text(text)
            for dependency in shlex.split(text.replace('\\\n',' '))[1:]:
                path=Path(dependency);path=(project/path).resolve() if not path.is_absolute() else path.resolve()
                if path.is_relative_to(source_root):continue
                report['dependencies'][str(path)]=sha(path)
                target=out/'toolchain-headers'/(sha(path)+'-'+path.name)
                if not target.exists():
                    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        for repetition in range(2):
            directory=out/f'build-{repetition+1}';directory.mkdir();target=directory/'i2c_knob.elf'
            # Same upstream sources, flags and linker script. Map output adds
            # observation only; no project source or makefile is changed.
            run([compiler,'-o',target,*sources,*flags,*config['original_link_flags'],f'-Wl,-Map,{directory/"image.map"}'],project)
            image=directory/'image.bin';run([prefix+'objcopy','-O','binary',target,image])
            nm=run([prefix+'nm','-n',target]);(directory/'symbols.txt').write_text(nm)
            sizes=run([prefix+'nm','-S','--size-sort',target]);(directory/'symbol-sizes.txt').write_text(sizes)
            objects={line.split()[3]:int(line.split()[1],16) for line in sizes.splitlines() if len(line.split())==4}
            (directory/'sections.txt').write_text(run([prefix+'size','-A',target]))
            (directory/'disasm.txt').write_text(run([prefix+'objdump','-d',target]))
            header,sections=elf.elf_sections(target.read_bytes());symbols=elf.symbols(nm)
            assert header[4]==symbols['jump_reset']==0
            for symbol in ['I2C1_EV_IRQHandler','I2C1_ER_IRQHandler','vectors','_eusrstack','_data_lma','_ebss']:assert symbol in symbols
            assert symbols['_eusrstack']==0x20000000+config['ram_bytes']
            data=sections['.data']['size'];bss=sections['.bss']['size'];size=image.stat().st_size
            assert size==symbols['_data_lma']+data<=config['flash_bytes'] and data+bss<=config['ram_bytes']
            row=dict(repetition=repetition+1,image_bytes=size,data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,
                     vector_bytes=objects['vectors'],init_section_bytes=sections['.init']['size'],entry=header[4],initial_stack_pointer=symbols['_eusrstack'],
                     image_sha256=sha(image),elf_sha256=sha(target),stack_peak_bytes=None,execution=False)
            report['results'].append(row);save()
        a,b=report['results'];assert a['image_sha256']==b['image_sha256'] and a['static_ram_bytes']==b['static_ram_bytes']
        provided=source_root/config['project']/'bin/i2c_knob.bin'
        report['provided_binary']=dict(bytes=provided.stat().st_size,sha256=sha(provided),matches_rebuild=sha(provided)==a['image_sha256'])
        for item in config['files']:assert sha(source_root/item['path'])==item['sha256']
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],builds=len(report['results']),error=report.get('error'))));return int(report['status']!='passed')


if __name__=='__main__':sys.exit(main())
