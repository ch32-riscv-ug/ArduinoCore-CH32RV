#!/usr/bin/env python3
"""Explicit EXP-01 reset/CRT link and bounded instruction-harness checks; no upload."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

REPO=Path(__file__).resolve().parents[3]
ROOT=Path(__file__).with_name('runtime')
MANIFEST=REPO/'planning/core-redesign/runtime-inputs.json'
spec=importlib.util.spec_from_file_location('boot_audit',ROOT/'inspect_boot.py')
boot=importlib.util.module_from_spec(spec);spec.loader.exec_module(boot)
CASES=[('empty',0,0),('empty-arrays-enabled',0,1),('state',1,0),('constructors',2,1),
       ('setup-loop',5,0),('gpio-raw',3,0),('gpio-fixed',4,0),('gpio-dynamic',6,0),('gpio-raw-dynamic',7,0),('unused-fixed',8,0)]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--sources',type=Path,help='pinned files; if omitted download hash-checked references into out')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3]
    config=json.loads(MANIFEST.read_text());commands=[]
    report=dict(status='running',scope=__doc__,results=[],negative_checks=[],inputs={},reference_sources=config['sources'])
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    def run(command,expect=True):
        entry=dict(command=[str(x) for x in command],expected_success=expect,returncode=None);commands.append(entry);save()
        r=subprocess.run(entry['command'],cwd=out,capture_output=True,text=True)
        entry.update(returncode=r.returncode,stdout=r.stdout,stderr=r.stderr);save()
        if (r.returncode==0)!=expect:raise RuntimeError(str(entry))
        return r
    save()
    try:
        for p in [Path(__file__).resolve(),MANIFEST,*ROOT.glob('*')]:
            if not p.is_file():continue
            relative=p.relative_to(REPO);dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
            report['inputs'][str(relative)]=sha(p)
        references=out/'references';references.mkdir()
        for record in config['sources']:
            target=references/record['file']
            if args.sources:shutil.copyfile(args.sources/record['file'],target)
            else:target.write_bytes(urllib.request.urlopen(record['url'],timeout=60).read())
            assert sha(target)==record['sha256'] and target.stat().st_size==record['bytes'],record
        report['compiler']=run([compiler,'--version']).stdout.splitlines()[0];report['compiler_sha256']=sha(compiler)
        report['tool_hashes']={}
        for name in ['cc1plus','as','ld','collect2']:
            path=Path(run([compiler,f'-print-prog-name={name}']).stdout.strip()).resolve();report['tool_hashes'][name]=sha(path)
        for name in ['objdump','objcopy','nm','size']:report['tool_hashes'][name]=sha(prefix+name)
        for profile in config['profiles']:
            for lto in [False,True]:
                group=out/profile['part']/f'lto{int(lto)}';group.mkdir(parents=True)
                flags=[f"-march={profile['march']}",f"-mabi={profile['abi']}",'-Os','-ffunction-sections','-fdata-sections',
                       '-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage']
                if lto:flags.append('-flto')
                def layout(dest,arrays):
                    text=(ROOT/'layout.ld.in').read_text()
                    for key,value in dict(FLASH=profile['flash_bytes'],RAM=profile['ram_bytes'],STACK=config['stack_reservation'],ARRAYS=arrays).items():text=text.replace('@'+key+'@',str(value))
                    (dest/'image.ld').write_text(text)
                def compile_case(dest,case,arrays):
                    dest.mkdir();layout(dest,arrays)
                    defines=[f"-DTARGET_X035={int(profile['x035'])}",f"-DVECTOR_COUNT={profile['vector_count']}",f'-DCRT_ARRAYS={arrays}',f'-DCASE={case}']
                    objects=[]
                    for source in ['reset.S','entry.cpp','sketch.cpp']:
                        obj=dest/(source+'.o');objects.append(obj)
                        extra=['-std=c++17'] if source.endswith('.cpp') else []
                        run([compiler,*flags,*extra,*defines,'-c',ROOT/source,'-o',obj])
                    return objects
                def link(dest,objects,expect=True,extra=()):
                    return run([compiler,*flags,'-nostdlib','-Wl,--gc-sections','-Wl,--no-warn-rwx-segments',f"-T{dest/'image.ld'}",
                                f"-Wl,-Map,{dest/'image.map'}",*extra,*objects,'-o',dest/'image.elf'],expect)
                for label,case,arrays in CASES:
                    dest=group/label;objects=compile_case(dest,case,arrays);link(dest,objects)
                    run([prefix+'objcopy','-O','binary',dest/'image.elf',dest/'image.bin'])
                    nm=run([prefix+'nm','-n',dest/'image.elf']).stdout;(dest/'symbols.txt').write_text(nm)
                    dis=run([prefix+'objdump','-d','-M','no-aliases,numeric',dest/'image.elf']).stdout;(dest/'disasm.txt').write_text(dis)
                    (dest/'sections.txt').write_text(run([prefix+'size','-A',dest/'image.elf']).stdout)
                    result=boot.inspect((dest/'image.elf').read_bytes(),(dest/'image.bin').read_bytes(),dis,nm,profile['ram_bytes'],profile['x035'])
                    result.update(part=profile['part'],lto=lto,case=label,elf_sha256=sha(dest/'image.elf'),image_sha256=sha(dest/'image.bin'))
                    if case==2:assert len(result['constructor_hooks'])==3
                    else:assert not result['constructor_hooks']
                    if case in [6,7]:assert 'requested_pin' in boot.symbols(nm)
                    if case in [3,4]:assert result['flash_bytes'] > next(r['flash_bytes'] for r in report['results'] if r['part']==profile['part'] and r['lto']==lto and r['case']=='empty')
                    report['results'].append(result);save()
                # These are expected rejections, not successful runtime behavior.
                dest=group/'reject-constructors';objects=compile_case(dest,2,0);r=link(dest,objects,False)
                assert 'constructor arrays disabled' in r.stderr
                report['negative_checks'].append(dict(part=profile['part'],lto=lto,case='constructors-without-runtime',rejected=True))
                for kind,size in [('ram',profile['ram_bytes']),('flash',profile['flash_bytes'])]:
                    dest=group/('reject-'+kind);objects=compile_case(dest,0,0)
                    source=dest/'overflow.cpp'
                    declaration=f'extern "C" {{ volatile unsigned char overflow[{size}]; }}' if kind=='ram' else f'extern "C" {{ extern const unsigned char overflow[{size}]; const unsigned char overflow[{size}] = {{1}}; }}'
                    source.write_text(declaration+'\n');obj=dest/'overflow.o';run([compiler,*flags,'-c',source,'-o',obj])
                    r=link(dest,[*objects,obj],False,['-Wl,--undefined=overflow'])
                    assert ('static RAM overlaps reserved stack' in r.stderr if kind=='ram' else 'FLASH' in r.stderr)
                    report['negative_checks'].append(dict(part=profile['part'],lto=lto,case=kind+'-overflow',rejected=True))
                save()
        report['status']='passed'
    except Exception as e:report.update(status='failed',error=str(e))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],builds=len(report['results']),negative_checks=len(report['negative_checks']),error=report.get('error'))))
    return int(report['status']!='passed')
if __name__=='__main__':sys.exit(main())
