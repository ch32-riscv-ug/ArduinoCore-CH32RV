"""Link pairs with exact pinned SDK scripts. No flashing or executable startup."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
INPUT=REPO/'planning/core-redesign/h417-layout-inputs.json'
MANIFEST=REPO/'planning/core-redesign/h417-inputs.json'
spec=importlib.util.spec_from_file_location('h417_layout',HERE/'h417_link_layout.py')
layout=importlib.util.module_from_spec(spec);spec.loader.exec_module(layout)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3]
    config=json.loads(INPUT.read_text());manifest=json.loads(MANIFEST.read_text())
    report=dict(status='running',inputs={},tool_hashes={},pairs=[],rejected=[],commands=[])
    def save(): (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(command,cwd=None,expect_failure=False):
        p=subprocess.run([str(v) for v in command],cwd=cwd or out,capture_output=True,text=True)
        item=dict(command=[str(v) for v in command],cwd=str(cwd or out),returncode=p.returncode,
                  stdout=p.stdout,stderr=p.stderr,expected_failure=expect_failure)
        report['commands'].append(item);save()
        if expect_failure:
            if not p.returncode or not any(v in p.stderr for v in ['overflowed','will not fit','overlaps']):
                raise ValueError('expected link capacity rejection missing')
        elif p.returncode: raise RuntimeError(str(item))
        return p.stdout
    try:
        scripts={}
        for path in [INPUT,MANIFEST,Path(__file__).resolve(),HERE/'h417_link_layout.py',
                     HERE/'h417_layout_entry.cpp',HERE/'h417_layout_shared.cpp']:
            relative=path.relative_to(REPO);dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,dest);report['inputs'][str(relative)]=sha(path)
        for core,settings in config['cores'].items():
            relative=settings['linker'];entry=next(e for e in manifest['sdk_files'] if e['path']==relative)
            path=args.bundle/'sdk'/relative
            if sha(path)!=entry['sha256']:raise ValueError('pinned SDK linker content mismatch')
            dest=out/'sdk'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
            scripts[core]=dest
            report['inputs']['sdk/'+relative]=sha(dest)
        report['compiler']=run([compiler,'--version']).splitlines()[0]
        report['tool_hashes']['compiler']=sha(compiler)
        for name in ['cc1plus','as','ld','collect2']:
            path=Path(run([compiler,f'-print-prog-name={name}']).strip()).resolve()
            report['tool_hashes'][name]=sha(path)
        for name in ['nm','objdump','readelf']:report['tool_hashes'][name]=sha(prefix+name)
        flags=[f'-march={config["march"]}',f'-mabi={config["abi"]}','-Os','-std=c++17',
               '-ffreestanding','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics',
               '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-msmall-data-limit=0']
        def pair(label,shared_bytes,lto,private_words=0,reject_core=None):
            root=out/label;shared=root/'V3F/obj/User/shared.o';shared.parent.mkdir(parents=True)
            run([compiler,*flags,f'-DSHARED_BYTES={shared_bytes}','-c',HERE/'h417_layout_shared.cpp','-o',shared])
            images={}
            for core_index,core in enumerate(config['cores']):
                cwd=root/core/'obj';cwd.mkdir(parents=True,exist_ok=True)
                extra=['-flto'] if lto else []
                run([compiler,*flags,*extra,f'-DCORE={core_index}',f'-DPRIVATE_WORDS={private_words if core==reject_core else 0}',
                     '-c',HERE/'h417_layout_entry.cpp','-o',cwd/'entry.o'])
                objects=[cwd/'entry.o']+([shared] if core=='V3F' else [])
                reject=core==reject_core or shared_bytes>config['shared_capacity']
                run([compiler,*flags,*extra,'-nostdlib','-Wl,--no-warn-rwx-segments',f'-T{scripts[core]}',
                     f'-Wl,-Map,{cwd/"image.map"}',*objects,'-o',cwd/'image.elf'],cwd,expect_failure=reject)
                if reject:
                    report['rejected'].append(dict(label=label,core=core,shared_bytes=shared_bytes,private_words=private_words));save()
                    return
                nm=run([prefix+'nm','-n','-S',cwd/'image.elf']);(cwd/'symbols.txt').write_text(nm)
                (cwd/'segments.txt').write_text(run([prefix+'readelf','-lW',cwd/'image.elf']))
                (cwd/'disasm.txt').write_text(run([prefix+'objdump','-d',cwd/'image.elf']))
                images[core]=layout.decode((cwd/'image.elf').read_bytes(),nm,(cwd/'image.map').read_text())
            checked=layout.validate_pair(images,config,shared_bytes)
            report['pairs'].append(dict(label=label,lto=lto,shared_bytes=shared_bytes,layout=checked,images=images));save()
        for count in config['shared_bytes']:
            for lto in [False,True]:pair(f'bytes{count}-lto{int(lto)}',count,lto)
        pair('shared-overflow',config['shared_capacity']+config['alignment'],False)
        for core,settings in config['cores'].items():
            pair(f'private-overflow-{core}',256,False,settings['regions']['RAM'][1]//4,core)
        report['status']='passed'
    except Exception as error:
        report.update(status='failed',error=str(error))
    finally:
        save()
        (out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*'))
                                                if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],pairs=len(report['pairs']),rejected=len(report['rejected']),error=report.get('error'))))
    return int(report['status']!='passed')


if __name__=='__main__':raise SystemExit(main())
