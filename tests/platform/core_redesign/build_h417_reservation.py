"""Derived reservation link pairs; no product linker edits, boot or upload."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).resolve().parent;REPO=HERE.parents[2]
INPUT=REPO/'planning/core-redesign/h417-reservation-inputs.json'
BASE=REPO/'planning/core-redesign/h417-layout-inputs.json'
MANIFEST=REPO/'planning/core-redesign/h417-inputs.json'
spec=importlib.util.spec_from_file_location('reservation_variants',HERE/'h417_reservation.py')
reservation=importlib.util.module_from_spec(spec);spec.loader.exec_module(reservation)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True);parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False);out=args.out.resolve()
    compiler=args.compiler.resolve();prefix=str(compiler)[:-3]
    settings=json.loads(INPUT.read_text());config=json.loads(BASE.read_text());manifest=json.loads(MANIFEST.read_text())
    report=dict(status='running',inputs={},tool_hashes={},pairs=[],rejected=[],mixed_rejected=[],commands=[])
    def save():(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(command,cwd=None,reject=False):
        p=subprocess.run([str(x) for x in command],cwd=cwd or out,capture_output=True,text=True)
        report['commands'].append(dict(command=[str(x) for x in command],cwd=str(cwd or out),returncode=p.returncode,
                                        stdout=p.stdout,stderr=p.stderr,expected_failure=reject));save()
        if reject:
            if not p.returncode or not any(s in p.stderr for s in ['overflowed','will not fit','capacity exceeded']):
                raise ValueError('expected reservation rejection missing')
        elif p.returncode:raise RuntimeError(str(report['commands'][-1]))
        return p.stdout
    try:
        sources=[INPUT,BASE,MANIFEST,Path(__file__).resolve(),HERE/'h417_reservation.py',HERE/'h417_link_layout.py',
                 HERE/'h417_reservation_entry.cpp',HERE/'h417_layout_shared.cpp']
        for path in sources:
            relative=path.relative_to(REPO);dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,dest);report['inputs'][str(relative)]=sha(path)
        sdk={}
        for core,core_config in config['cores'].items():
            name=core_config['linker'];entry=next(e for e in manifest['sdk_files'] if e['path']==name)
            path=args.bundle/'sdk'/name
            if sha(path)!=entry['sha256']:raise ValueError('pinned SDK linker mismatch')
            dest=out/'sdk'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
            report['inputs']['sdk/'+name]=sha(path);sdk[core]=path.read_text()
        report['compiler']=run([compiler,'--version']).splitlines()[0];report['tool_hashes']['compiler']=sha(compiler)
        for name in ['cc1plus','as','ld','collect2']:
            path=Path(run([compiler,f'-print-prog-name={name}']).strip()).resolve();report['tool_hashes'][name]=sha(path)
        for name in ['nm','readelf','objdump']:report['tool_hashes'][name]=sha(prefix+name)
        flags=[f'-march={config["march"]}',f'-mabi={config["abi"]}','-Os','-std=c++17','-ffreestanding',
               '-fno-builtin','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-unwind-tables',
               '-fno-asynchronous-unwind-tables','-msmall-data-limit=0']
        def pair(capacity,lto,reject_core=None):
            label=f'capacity{capacity}-lto{int(lto)}'+(f'-overflow-{reject_core}' if reject_core else '')
            root=out/label;root.mkdir();payload=settings['payload_bytes'] if capacity else 0
            actual=capacity+settings['alignment'] if reject_core else payload
            shared=root/'V3F/obj/User/shared.o'
            if actual:
                shared.parent.mkdir(parents=True)
                run([compiler,*flags,f'-DSHARED_BYTES={actual}','-c',HERE/'h417_layout_shared.cpp','-o',shared])
            images={}
            for index,core in enumerate(config['cores']):
                if reject_core and core!=reject_core:continue
                cwd=root/core/'obj';cwd.mkdir(parents=True,exist_ok=True)
                script=cwd/'derived.ld';script.write_text(reservation.derive(sdk[core],core,config,capacity,settings['layout_version']))
                extra=['-flto'] if lto else []
                run([compiler,*flags,*extra,f'-DCORE={index}',f'-DSHARED_ACTIVE={int(capacity>0)}',
                     '-c',HERE/'h417_reservation_entry.cpp','-o',cwd/'entry.o'])
                objects=[cwd/'entry.o']
                if actual and (core=='V3F' or capacity==0):objects.append(shared)
                run([compiler,*flags,*extra,'-nostdlib','-Wl,--no-warn-rwx-segments',f'-T{script}',
                     f'-Wl,-Map,{cwd/"image.map"}',*objects,'-o',cwd/'image.elf'],cwd,reject=bool(reject_core))
                if reject_core:
                    report['rejected'].append(dict(capacity=capacity,core=core,payload_bytes=actual));save();return
                nm=run([prefix+'nm','-n','-S',cwd/'image.elf']);(cwd/'symbols.txt').write_text(nm)
                (cwd/'segments.txt').write_text(run([prefix+'readelf','-lW',cwd/'image.elf']))
                (cwd/'disasm.txt').write_text(run([prefix+'objdump','-d',cwd/'image.elf']))
                images[core]=reservation.base.decode((cwd/'image.elf').read_bytes(),nm,(cwd/'image.map').read_text())
            checked=reservation.validate(images,config,capacity,payload,settings['layout_version'])
            selected=reservation.geometry(config,capacity)
            for row in checked:
                row.update(reserved_bytes=capacity,normal_ram_bytes=selected['cores'][row['core']]['regions']['RAM'][1])
            report['pairs'].append(dict(capacity=capacity,lto=lto,payload_bytes=payload,layout=checked,images=images));save()
        for capacity in settings['capacities']:
            for lto in [False,True]:pair(capacity,lto)
            for core in config['cores']:pair(capacity,False,core)
        def mixed(a,b,expected):
            ia=next(p for p in report['pairs'] if p['capacity']==a and not p['lto'])['images']['V3F']
            ib=next(p for p in report['pairs'] if p['capacity']==b and not p['lto'])['images']['V5F']
            try:reservation.validate(dict(V3F=ia,V5F=ib),config,expected,settings['payload_bytes'] if expected else 0)
            except ValueError as error:
                report['mixed_rejected'].append(dict(v3_capacity=a,v5_capacity=b,expected=expected,diagnostic=str(error)));save()
            else:raise ValueError('mixed configuration accepted')
        mixed(256,1024,256);mixed(0,256,0);mixed(256,0,256)
        report['status']='passed'
    except Exception as error:report.update(status='failed',error=str(error))
    finally:
        save();(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*'))
                                                       if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],pairs=len(report['pairs']),rejected=len(report['rejected']),
                         mixed_rejected=len(report['mixed_rejected']),error=report.get('error'))))
    return int(report['status']!='passed')


if __name__=='__main__':raise SystemExit(main())
