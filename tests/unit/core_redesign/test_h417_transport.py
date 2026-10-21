"""N06 concurrent shared transport and core-local normal executors."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

HERE=Path(__file__).parent
REPO=HERE.parents[2]
CONFIG=json.loads((REPO/'planning/core-redesign/h417-transport-inputs.json').read_text())


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    sanitizer=os.environ.get('EXP06_H417_TRANSPORT_SANITIZER','address,undefined');assert sanitizer in CONFIG['sanitizers']
    directory=tmp_path_factory.mktemp('h417-transport');out=None
    paths=[Path(__file__),REPO/'planning/core-redesign/h417-transport-inputs.json']
    paths += [HERE/n for n in ['h417_transport.hpp','h417_endpoint.hpp','h417_transport_driver.cpp','h417_features.hpp','event_executor.hpp','work_executor.hpp','data_parts.hpp']]
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    report=dict(sanitizer=sanitizer,compiler_sha256=sha(compiler),compiler_version=subprocess.check_output([compiler,'--version'],text=True),inputs={str(p.relative_to(REPO)):sha(p) for p in paths},builds=[])
    if destination:=os.environ.get('EXP06_H417_TRANSPORT_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
    binaries={}
    for features in CONFIG['features']:
        binary=directory/f'app-{features}'
        command=[compiler,'-std=c++17','-O1','-g1','-pthread','-Wall','-Wextra','-Werror',f'-fsanitize={sanitizer}',
                 '-fno-sanitize-recover=all','-fno-omit-frame-pointer',f'-DFEATURES={features}',str(HERE/'h417_transport_driver.cpp'),'-o',str(binary)]
        r=subprocess.run(command,capture_output=True,text=True,timeout=300)
        b=dict(features=features,command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr)
        if not r.returncode:b['executable_sha256']=sha(binary)
        report['builds'].append(b)
        if out:
            (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
            if not r.returncode:shutil.copy2(binary,out/binary.name)
        assert r.returncode==0,r.stderr
        binaries[features]=binary
    return binaries


def run(native,features,gate,candidate,policy=1,seed=0,mode='stress'):
    args=[str(native[features]),mode,gate,str(candidate),str(policy),str(seed),str(CONFIG['messages'])]
    p=subprocess.run(args,capture_output=True,text=True,timeout=90)
    if destination:=os.environ.get('EXP06_H417_TRANSPORT_EVIDENCE'):
        d=dict(features=features,gate=gate,candidate=candidate,policy=policy,seed=seed,mode=mode,args=args,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        key=f'{mode}-f{features}-{gate}-c{candidate}-p{policy}-s{seed}'
        Path(destination,f'{key}.json').write_text(json.dumps(d,indent=2)+'\n')
    assert p.returncode==0,p.stderr;assert not p.stderr
    return json.loads(p.stdout)


def fnv(values):
    result=2166136261
    for value in values:result=((result^value)*16777619)&0xffffffff
    return result


@pytest.mark.parametrize('features',range(4))
@pytest.mark.parametrize('gate',['mutex','atomic'])
@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('policy',range(3))
@pytest.mark.parametrize('seed',[0,1])
def test_concurrent_fifo_payloads_private_endpoints_and_release(native,features,gate,candidate,policy,seed):
    r=run(native,features,gate,candidate,policy,seed)
    assert r['session'][0:4]==[1,0,3,0] and r['session'][6:]==[0,0]
    assert r['fabric'][6:]==[0,0] and all(phase==0 for phase in r['phases'])
    messages=sorted(r['messages'][0]+r['messages'][1]);blocks=sorted(r['blocks'][0]+r['blocks'][1])
    assert [record[1] for record in messages]==list(range(seed,seed+CONFIG['messages'])) if features&1 else messages==[]
    assert [record[1] for record in blocks]==list(range(1,CONFIG['blocks']+1)) if features&2 else blocks==[]
    for _,generation,hash_value in blocks:
        assert hash_value==fnv([(seed+generation-1+i)&255 for i in range(64)])
    assert sum(ep[3] for ep in r['endpoints'])==len(messages)
    assert sum(ep[8] for ep in r['endpoints'])==len(blocks)
    assert sum(ep[7] for ep in r['endpoints'])==len(blocks)*64
    assert all(not ep[0] and not ep[5] for ep in r['endpoints'])
    assert r['attempts']>0 and 0<=r['busy']<r['attempts']


@pytest.mark.parametrize('features',range(4))
@pytest.mark.parametrize('gate',['mutex','atomic'])
@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('mode',['races','cleanup'])
def test_recovery_waits_for_active_callback_and_retryable_cleanup(native,features,gate,candidate,mode):
    r=run(native,features,gate,candidate,mode=mode)
    assert r['before_active']==2
    assert r['session'][0:4]==[2,0,3,0] and r['session'][6:]==[0,0]
    ep=r['endpoint'];assert not ep[0] and not ep[5]
    if features&1:assert ep[3]==(2 if mode=='races' else 1)
    if features&2:
        assert ep[8:10]==[1,fnv([fnv(range(90,154))])]
        assert ep[7]==(65 if features==2 and mode=='races' else 64)
        assert ep[10]==int(features==2 and mode=='races')
