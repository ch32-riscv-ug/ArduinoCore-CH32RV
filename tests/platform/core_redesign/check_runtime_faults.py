#!/usr/bin/env python3
"""Negative controls for EXP-01's instruction harness, without running a device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).with_name('runtime')
spec=importlib.util.spec_from_file_location('fault_boot_audit',ROOT/'inspect_boot.py')
boot=importlib.util.module_from_spec(spec);spec.loader.exec_module(boot)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--evidence',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve();evidence=args.evidence.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3];report=dict(status='running',results=[],commands=[])
    def save():(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(command):
        r=subprocess.run([str(x) for x in command],capture_output=True,text=True)
        report['commands'].append(dict(command=[str(x) for x in command],returncode=r.returncode,stdout=r.stdout,stderr=r.stderr));save()
        assert r.returncode==0,r.stderr;return r.stdout
    save()
    try:
        base=json.loads((evidence/'result.json').read_text());assert base['status']=='passed' and len(base['results'])==40
        assert sha(compiler)==base['compiler_sha256']
        config=json.loads((evidence/'inputs/planning/core-redesign/runtime-inputs.json').read_text())
        source=evidence/'inputs/tests/platform/core_redesign/runtime/reset.S'
        assert sha(source)==base['inputs']['tests/platform/core_redesign/runtime/reset.S']
        assert sha(ROOT/'inspect_boot.py')==base['inputs']['tests/platform/core_redesign/runtime/inspect_boot.py']
        for name in ['inspect_boot.py']:
            shutil.copyfile(ROOT/name,out/name)
        shutil.copyfile(Path(__file__),out/Path(__file__).name)
        report['source_evidence_sha256']=sha(evidence/'result.json')
        faults=[('data-zeroed','state',1,0,'sw t0, 0(a1)','sw zero, 0(a1)'),
                ('bss-not-zero','state',1,0,'sw zero, 0(a0)','sw a2, 0(a0)'),
                ('preinit-skipped','constructors',2,1,'bgeu s0, s1, .Linit_begin','j .Linit_begin')]
        for profile in config['profiles']:
            for lto in [False,True]:
                for name,label,case,arrays,old,new in faults:
                    dest=out/profile['part']/f'lto{int(lto)}'/name;dest.mkdir(parents=True)
                    original=evidence/profile['part']/f'lto{int(lto)}'/label
                    text=source.read_text();assert text.count(old)==1; (dest/'reset.S').write_text(text.replace(old,new))
                    for f in ['entry.cpp.o','sketch.cpp.o','image.ld']:shutil.copyfile(original/f,dest/f)
                    flags=[f"-march={profile['march']}",f"-mabi={profile['abi']}",'-Os','-ffunction-sections','-fdata-sections']
                    if lto:flags.append('-flto')
                    run([compiler,*flags,f"-DTARGET_X035={int(profile['x035'])}",f"-DVECTOR_COUNT={profile['vector_count']}",f'-DCRT_ARRAYS={arrays}',f'-DCASE={case}','-c',dest/'reset.S','-o',dest/'reset.o'])
                    run([compiler,*flags,'-nostdlib','-Wl,--gc-sections','-Wl,--no-warn-rwx-segments',f"-T{dest/'image.ld'}",dest/'reset.o',dest/'entry.cpp.o',dest/'sketch.cpp.o','-o',dest/'image.elf'])
                    run([prefix+'objcopy','-O','binary',dest/'image.elf',dest/'image.bin'])
                    nm=run([prefix+'nm','-n',dest/'image.elf']);(dest/'symbols.txt').write_text(nm)
                    dis=run([prefix+'objdump','-d','-M','no-aliases,numeric',dest/'image.elf']);(dest/'disasm.txt').write_text(dis)
                    rejected=False
                    try:boot.inspect((dest/'image.elf').read_bytes(),(dest/'image.bin').read_bytes(),dis,nm,profile['ram_bytes'],profile['x035'])
                    except AssertionError as error:rejected=True;reason=repr(error)
                    assert rejected,('broken startup passed',name)
                    report['results'].append(dict(part=profile['part'],lto=lto,fault=name,rejected=True,reason=reason));save()
        report['status']='passed'
    except Exception as e:report.update(status='failed',error=repr(e))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(x.relative_to(out)):sha(x) for x in sorted(out.rglob('*')) if x.is_file() and x.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],faults=len(report['results']),error=report.get('error'))))
    return int(report['status']!='passed')
if __name__=='__main__':raise SystemExit(main())
