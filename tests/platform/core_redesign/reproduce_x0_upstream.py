#!/usr/bin/env python3
"""Build the frozen upstream PD adapter twice, without running or uploading it."""
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
INPUT=ROOT/'planning/core-redesign/x0-upstream-inputs.json'
spec=importlib.util.spec_from_file_location('upstream_elf',Path(__file__).parent/'runtime/inspect_boot.py')
elf=importlib.util.module_from_spec(spec);spec.loader.exec_module(elf)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_protocol(name,a,b,value):
    def write(address,data):return dict(address=address,tx=data,rx=[],ack=[])
    def cursor(x,y):return [0,0x22,y,y+1,x&15,0x10|(x>>4),0xb0|y]
    transactions=value['transactions']
    if name=='init':assert transactions==[[write(0x78,[0,0xa8,0x1f,0x20,1,0xda,2,0x8d,0x14,0xaf])]]
    elif name=='cursor':assert transactions==[[write(0x78,cursor(a,b))]]
    elif name in ['clear-line','clear']:
        expected=[]
        for y in ([a] if name=='clear-line' else [0,2]):expected += [[write(0x78,cursor(0,y))],[write(0x78,[0x40]+[0]*256)]]
        assert transactions==expected
    elif name in ['value','pps']:
        assert len(transactions)==len(transactions[0])==1
        leg=transactions[0][0];data=leg['tx'];characters=5 if name=='value' else 6
        assert leg['address']==0x78 and not leg['rx'] and not leg['ack'] and len(data)==1+16*characters and data[0]==0x40
        for i in range(characters):assert data[1+i*16:7+i*16]==[0]*6
        # Glyph bytes are observed, not validated by a duplicated font table.
    elif name=='ina-init':assert transactions==[[write(0x80,[0,0x27,0xff])],[write(0x80,[5,0x10,0])]]
    else:
        register=a if name=='ina-read' else 2 if name=='ina-v' else 4
        assert transactions==[[write(0x80,[register]),dict(address=0x81,tx=[],rx=[b>>8,b&255],ack=[1,0])]]
        expected=b if name=='ina-read' else (b>>1)&0xfffc if name=='ina-v' else b if b<32768 else b-65536
        assert value['result']==expected


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    source=parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--source-root',type=Path,help='Root with the locked upstream paths, including LICENSE')
    source.add_argument('--fetch',action='store_true',help='Explicitly download only the fixed URLs into the new output directory')
    parser.add_argument('--native-compiler',type=Path,help='Optionally record original OLED/INA calls using a host I2C substitute')
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
        for path in [INPUT,Path(__file__).resolve(),Path(__file__).parent/'runtime/inspect_boot.py',ROOT/'tests/unit/core_redesign/x0_upstream_protocol_driver.c']:
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
            directory=out/f'build-{repetition+1}';directory.mkdir();target=directory/'pd_adapter.elf'
            # Same upstream sources, flags and linker script. Map output adds
            # observation only; no project source or makefile is changed.
            run([compiler,'-o',target,*sources,*flags,*config['original_link_flags'],f'-Wl,-Map,{directory/"image.map"}'],project)
            image=directory/'image.bin';run([prefix+'objcopy','-O','binary',target,image])
            nm=run([prefix+'nm','-n',target]);(directory/'symbols.txt').write_text(nm)
            (directory/'symbol-sizes.txt').write_text(run([prefix+'nm','-S','--size-sort',target]))
            (directory/'sections.txt').write_text(run([prefix+'size','-A',target]))
            (directory/'disasm.txt').write_text(run([prefix+'objdump','-d',target]))
            header,sections=elf.elf_sections(target.read_bytes());symbols=elf.symbols(nm)
            assert header[4]==symbols['jump_reset']==0
            for symbol in ['SysTick_Handler','USBPD_IRQHandler','_eusrstack','_data_lma','_ebss']:assert symbol in symbols
            assert symbols['_eusrstack']==0x20000000+config['ram_bytes']
            data=sections['.data']['size'];bss=sections['.bss']['size'];size=image.stat().st_size
            assert size==symbols['_data_lma']+data<=config['flash_bytes'] and data+bss<=config['ram_bytes']
            row=dict(repetition=repetition+1,image_bytes=size,data_bytes=data,bss_bytes=bss,static_ram_bytes=data+bss,
                     vector_bytes=sections['.vector']['size'],entry=header[4],initial_stack_pointer=symbols['_eusrstack'],
                     image_sha256=sha(image),elf_sha256=sha(target),stack_peak_bytes=None,execution=False)
            report['results'].append(row);save()
        a,b=report['results'];assert a['image_sha256']==b['image_sha256'] and a['static_ram_bytes']==b['static_ram_bytes']
        provided=source_root/config['project']/'bin/pd_adapter.bin'
        report['provided_binary']=dict(bytes=provided.stat().st_size,sha256=sha(provided),matches_rebuild=sha(provided)==a['image_sha256'])
        if args.native_compiler:
            native=args.native_compiler.resolve();directory=out/'native';directory.mkdir();executable=directory/'recorder'
            driver=ROOT/'tests/unit/core_redesign/x0_upstream_protocol_driver.c'
            native_flags=['-std=gnu11','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DF_CPU=48000000']
            run([native,*native_flags,'-I',project,'-I',project/'src',driver,project/'src/oled.c',project/'src/ina219.c','-o',executable])
            report['native']=dict(compiler=str(native),compiler_sha256=sha(native),compiler_version=run([native,'--version']).splitlines()[0],executable_sha256=sha(executable),cases=[])
            cases=[('init',0,0),('cursor',0,0),('cursor',36,1),('cursor',127,2),('clear-line',0,0),('clear-line',2,0),('clear',0,0),('value',0,0),('value',12345,0),('value',65535,0),('pps',0,0),('ina-init',0,0),('ina-read',2,0x1234),('ina-read',4,0xff9c),('ina-v',0,0x5dc0),('ina-i',0,0xff9c)]
            for name,a,b in cases:
                text=run([executable,name,a,b]);assert not commands[-1]['stderr'];value=json.loads(text)
                validate_protocol(name,a,b,value)
                report['native']['cases'].append(dict(name=name,a=a,b=b,observed=value));save()
        for item in config['files']:assert sha(source_root/item['path'])==item['sha256']
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],builds=len(report['results']),error=report.get('error'))));return int(report['status']!='passed')


if __name__=='__main__':sys.exit(main())
