"""N03 single-core IRQ exchange and serial K0 meaning; no slave register backend."""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess

import pytest

UNIT=Path(__file__).parent
REPO=UNIT.parents[2]
spec=importlib.util.spec_from_file_location('shared_k0_reference',UNIT/'test_k0_candidates.py')
reference=importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)
CANDIDATES=['R','A','B','C-bridge']


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('k0-shared')
    command=[compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(UNIT/'k0_shared_driver.cpp'),'-o',str(directory/'app')]
    result=subprocess.run(command,capture_output=True,text=True,timeout=240)
    if destination:=os.environ.get('EXP06_K0_SHARED_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        paths=[*[UNIT/name for name in ['k0_shared.hpp','k0_shared_driver.cpp','k0_candidates.hpp','data_parts.hpp',
               'work_executor.hpp','event_executor.hpp','test_k0_candidates.py','oracle.py','traces.json']],Path(__file__),
               REPO/'planning/core-redesign/k0-shared-inputs.json',REPO/'planning/core-redesign/workloads.ja.md']
        for path in paths:
            target=out/'inputs'/path.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        report=dict(command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr,
                    compiler_sha256=sha(compiler),compiler_version=subprocess.check_output([compiler,'--version'],text=True),
                    inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if result.returncode==0:
            shutil.copy2(directory/'app',out/'app');report['executable_sha256']=sha(directory/'app')
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert result.returncode==0,result.stderr
    return directory/'app'


def run(native,commands,candidate='R',capacity=1,mode='guarded',domain=0,opted=False,serial=False):
    result=subprocess.run([str(native),mode,candidate,str(capacity),str(domain),str(int(opted))],
                          input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stderr;assert not result.stderr
    rows=[json.loads(line) for line in result.stdout.splitlines()];assert len(rows)==len(commands)
    if serial:
        wanted=reference.expected(commands,capacity,domain,opted)
        for index,(row,expected) in enumerate(zip(rows,wanted)):
            assert row['state']==expected,(candidate,index,commands[index],row,expected)
    has_external_mask=any(c.startswith('mask ') for c in commands)
    for row in rows:
        assert row['gate'][0:3]==[0,0,False] if not has_external_mask else row['gate'][1]==0
    if output:=os.environ.get('EXP06_K0_SHARED_EVIDENCE'):
        key=hashlib.sha256(json.dumps([mode,candidate,capacity,domain,opted,commands]).encode()).hexdigest()
        Path(output,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(dict(mode=mode,candidate=candidate,capacity=capacity,
            domain=domain,opted=opted,commands=commands,rows=rows),separators=(',',':')).encode(),mtime=0))
    return rows


def frame(sequence,increment=1,lower=-1000,upper=1000):
    return ['begin_write',*(f'byte {value}' for value in struct.pack('<Hhhh',sequence,increment,lower,upper)),'stop']


@pytest.mark.parametrize('trace',reference.TRACES,ids=lambda t:t['id'])
@pytest.mark.parametrize('candidate',CANDIDATES)
def test_frozen_serial_contract(native,trace,candidate):
    run(native,['begin',*trace['commands']],candidate,serial=True)


@pytest.mark.parametrize('candidate',CANDIDATES)
@pytest.mark.parametrize('capacity',[1,2])
def test_capacity_retry_invalid_and_snapshot_still_match(native,candidate,capacity):
    commands=['begin',*frame(1,2),*frame(2,3),'input 2','step','step',*frame(3,4),'input 1','step',
              'begin_read',*['read']*4,'input 5','step',*['read']*4,*frame(4,0),'step']
    run(native,commands,candidate,capacity,serial=True)


@pytest.mark.parametrize('domain,opted',[(0,False),(1,False),(1,True)])
def test_progress_profiles_and_contexts(native,domain,opted):
    commands=['opportunity 0 0 0','begin','input 3']
    commands += [f'opportunity 0 2 {context}' for context in [1,2,3]]
    for where in [1,2]:
        for profile in [0,1,2]:commands += ['input 1',f'opportunity {where} {profile} 0']
    commands += ['step','begin_read',*['read']*8]
    for candidate in CANDIDATES:run(native,commands,candidate,domain=domain,opted=opted,serial=True)


@pytest.mark.parametrize('candidate',CANDIDATES)
def test_lost_delta_negative_control_and_deferred_arrival(native,candidate):
    commands=['begin','input 1','arm 1 1 0 2','step','step']
    safe=run(native,commands,candidate)
    unsafe=run(native,commands,candidate,mode='unguarded')
    assert safe[-2]['state'][0]==1 and safe[-2]['state'][6]==2
    assert safe[-1]['state'][0]==3 and unsafe[-1]['state'][0]==1
    assert safe[-1]['gate'][4:7]==[1,1,1] and unsafe[-1]['gate'][4:7]==[1,1,0]


@pytest.mark.parametrize('candidate',CANDIDATES)
@pytest.mark.parametrize('byte',range(8))
def test_snapshot_publication_is_atomic_under_declared_irq_gate(native,candidate,byte):
    commands=['begin','input 255','step','input 1',f'arm 3 {byte} 1 0','step',*['read']*8]
    safe=run(native,commands,candidate)
    assert safe[-1]['held']==list((256).to_bytes(4,'little')+bytes(4))
    assert safe[-1]['gate'][4:7]==[1,1,1]
    if byte==0:
        unsafe=run(native,commands,candidate,mode='unguarded')
        assert unsafe[-1]['held'][:4]==[0,0,0,0]
        assert unsafe[-1]['held'][:4] not in [list((255).to_bytes(4,'little')),list((256).to_bytes(4,'little'))]


@pytest.mark.parametrize('candidate',CANDIDATES)
def test_completed_config_during_normal_take_is_not_lost(native,candidate):
    second=frame(2,3)
    commands=['begin',*frame(1,2),*second[:-1],'input 1','arm 1 0 3 0','step','step']
    rows=run(native,commands,candidate,capacity=2)
    assert rows[-2]['state'][:5]==[2,2,-1000,1000,1]
    assert rows[-2]['state'][7]==1 and rows[-1]['state'][4]==2


@pytest.mark.parametrize('candidate',CANDIDATES)
def test_irq_after_idle_check_remains_for_next_explicit_opportunity(native,candidate):
    commands=['begin','arm 0 0 0 1','step','step']
    rows=run(native,commands,candidate)
    assert rows[-1]['state'][0]==1 and rows[-1]['state'][6]==0
    assert rows[-1]['gate'][4:7]==[1,1,1]


def test_nested_gate_restores_previously_disabled_state(native):
    rows=run(native,['mask 1','nested','input 1','mask 0','begin','step'])
    assert rows[1]['gate'][:2]==[True,0] and rows[2]['gate'][:2]==[True,0]
    assert rows[-1]['state'][0]==1


@pytest.mark.parametrize('seed',range(10))
def test_seeded_irq_arrivals_preserve_all_input_without_dispatch_in_gate(native,seed):
    rng=random.Random(seed)
    commands=['begin',*frame(1,1,-32768,32767),'step']
    boundaries=[(0,0),(1,0),(1,1),(1,2),(2,0),*((3,i) for i in range(8)),(4,0)]
    for _ in range(10000):
        tag,index=rng.choice(boundaries)
        commands += ['input 1',f'arm {tag} {index} 0 1','step']
    commands += ['step','step']
    for candidate in CANDIDATES:
        rows=run(native,commands,candidate)
        assert rows[-1]['state'][0]==20000 and rows[-1]['state'][6]==0
        assert rows[-1]['gate'][4:6]==[10000,10000]
