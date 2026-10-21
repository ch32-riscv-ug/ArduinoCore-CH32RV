"""Admission reserves terminal storage; bus ownership ends before collection."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;REPO=UNIT.parents[2]
INPUT=REPO/'planning/core-redesign/completion-arbiter-inputs.json';CONFIG=json.loads(INPUT.read_text())
spec=importlib.util.spec_from_file_location('completion_reference',UNIT/'completion_arbiter_oracle.py')
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    exe=tmp_path_factory.mktemp('completion-arbiter')/'control'
    flags=['-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    command=[compiler,*flags,str(UNIT/'completion_arbiter_driver.cpp'),'-o',str(exe)]
    p=subprocess.run(command,capture_output=True,text=True,timeout=180)
    if destination:=os.environ.get('EXP03_COMPLETION_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        paths=[UNIT/n for n in ['completion_arbiter.hpp','completion_arbiter_driver.cpp',
                               'completion_arbiter_oracle.py','io_lifecycle.hpp',Path(__file__).name]]+[INPUT]
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        for path in paths:
            dest=out/'inputs'/path.relative_to(REPO);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
        report=dict(command=command,compiler=compiler,compiler_sha256=sha(compiler),returncode=p.returncode,
                    stdout=p.stdout,stderr=p.stderr,inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if p.returncode==0:shutil.copy2(exe,out/'native-control');report['binary_sha256']=sha(exe)
        (out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    return exe


def replay(native,commands,policy,short=False):
    args=[str(native),policy,'short' if short else 'normal'];stdin='\n'.join(commands)+'\n'
    p=subprocess.run(args,input=stdin,capture_output=True,text=True,timeout=60)
    if destination:=os.environ.get('EXP03_COMPLETION_EVIDENCE'):
        key=hashlib.sha256(json.dumps([args[1:],stdin]).encode()).hexdigest()
        record=dict(args=args[1:],stdin=stdin,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        path=Path(destination)/f'trace-{key}.json'
        if path.exists():assert json.loads(path.read_text())==record
        else:path.write_text(json.dumps(record,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];assert len(rows)==len(commands)
    model=reference.Model(policy=='dedicated',short)
    for n,(command,row) in enumerate(zip(commands,rows)):
        assert row==model.apply(command),(n,command,row)
        assert sum(r[0]==2 for r in row['records'])<=1
        assert (row['current']<len(row['records']))==any(r[0]==2 for r in row['records'])
    return rows


@pytest.mark.parametrize('policy',CONFIG['policies'])
def test_reservation_and_buffer_loan_precede_dispatch(policy,native):
    rows=replay(native,['submit 0 0 8 0 60','open 0','open 1','open 2',
        'submit 0 0 8 0 60','submit 1 1 8 1 60','submit 2 2 8 0 60',
        'write 0 0 99','read 1 0','finish 1 0 0 1 8 5 1','dispatch'],policy)
    assert rows[0]['status']==4 and rows[4]['status']==rows[5]['status']==0
    assert rows[6]['status']==(0 if policy=='dedicated' else 1)
    assert not rows[7]['status'] and not rows[8]['status'] and not rows[9]['status']
    assert rows[-1]['token']==[1,0,0] and rows[-1]['records'][0][0]==2


@pytest.mark.parametrize('policy',CONFIG['policies'])
def test_terminal_result_reserves_record_but_bus_can_serve_next_client(policy,native):
    commands=['open 0','open 1','open 2','submit 0 0 4 1 60','submit 1 1 4 1 60','dispatch',
              'finish 1 0 0 1 4 10 0','dispatch','finish 1 0 0 1 4 10 1',
              'notice 1 0 0','ack 1 0 0','submit 0 2 4 0 60','dispatch',
              'finish 1 1 1 1 4 30 1','ack 1 1 1','submit 2 2 4 0 60','take 1 0 0',
              'submit 0 0 4 0 60','take 1 0 0','notice 1 0 0']
    rows=replay(native,commands,policy)
    assert not rows[6]['status'] and not rows[7]['status']
    assert rows[10]['status']==1 and rows[10]['records'][0][0]==3
    assert rows[11]['status']==1 and rows[12]['token']==[1,1,1]
    assert rows[15]['status']==(0 if policy=='dedicated' else 1)
    assert rows[16]['buffers'][0][:4]==[10,11,12,13]
    assert not rows[-2]['status'] and not rows[-1]['status']


@pytest.mark.parametrize('policy',CONFIG['policies'])
def test_queued_cancel_is_local_and_running_cancel_waits_for_stopped_terminal(policy,native):
    commands=['open 0','open 1','submit 0 0 8 0 60','submit 1 1 8 1 60','end 0','notice 1 0 0','open 0',
              'dispatch','cancel 1 1 1','take 1 1 1','finish 1 1 1 3 0 0 0',
              'dispatch','finish 1 1 1 3 0 0 1','end 1','notice 1 1 1','take 1 0 0',
              'open 0','take 1 1 1','open 1']
    rows=replay(native,commands,policy)
    assert rows[4]['records'][0][:4]==[3,0,1,0] and not rows[5]['status'] and not rows[6]['status']
    assert rows[7]['token']==[1,1,1] and not rows[9]['status'] and not rows[10]['status']
    assert not rows[11]['status'] and rows[12]['status']==1
    assert rows[-1]['status']==1 and all(r[0]==0 for r in rows[-1]['records'])


@pytest.mark.parametrize('policy',CONFIG['policies'])
def test_round_robin_does_not_let_just_collected_client_jump_a_waiting_client(policy,native):
    commands=['open 0','open 1','submit 0 0 1 0 60','submit 1 1 1 0 60','dispatch',
              'finish 1 0 0 1 1 0 1','take 1 0 0','submit 0 0 1 0 60','dispatch',
              'finish 1 1 1 1 1 0 1','take 1 1 1','dispatch']
    rows=replay(native,commands,policy)
    assert [row['token'][2] for command,row in zip(commands,rows) if command=='dispatch']==[0,1,0]


def test_three_continuously_ready_dedicated_clients_rotate_and_drain(native):
    commands=[f'open {c}' for c in range(3)]+[f'submit {c} {c} 1 0 60' for c in range(3)]
    for n in range(6):
        c=n%3;generation=n//3+1
        commands+=['dispatch',f'finish {generation} {c} {c} 1 1 0 1',f'take {generation} {c} {c}',f'submit {c} {c} 1 0 60']
    commands += [f'end {c}' for c in range(3)]+[f'take 3 {c} {c}' for c in range(3)]
    rows=replay(native,commands,'dedicated')
    assert [r['token'][2] for cmd,r in zip(commands,rows) if cmd=='dispatch']==[0,1,2,0,1,2]
    assert all(r[0]==0 for r in rows[-1]['records'])


def test_pool_round_robin_does_not_guarantee_admission_to_a_third_client(native):
    commands=['open 0','open 1','open 2','submit 0 0 1 0 60','submit 1 1 1 0 60']
    for n in range(12):
        c=n%2;g=n//2+1
        commands+=['dispatch',f'finish {g} {c} {c} 1 1 0 1',f'take {g} {c} {c}',
                   f'submit {c} {c} 1 0 60','submit 2 2 1 0 60']
    blocked=len(commands)-1
    commands+=['end 0','end 1','end 2','take 7 0 0','take 7 1 1']
    rows=replay(native,commands,'pooled')
    attempts=[r for cmd,r in zip(commands,rows) if cmd=='submit 2 2 1 0 60']
    assert len(attempts)==12 and all(r['status']==1 for r in attempts)
    assert all(r['records'][0][1]!=2 and r['records'][1][1]!=2 for r in attempts)
    assert rows[blocked]['buffers'][2]==[0]*32
    assert all(r[0]==0 for r in rows[-1]['records'])


@pytest.mark.parametrize('policy',CONFIG['policies'])
def test_duplicate_buffer_and_invalid_terminal_preserve_running_and_outputs(policy,native):
    commands=['open 0','open 1','submit 0 0 4 0 60','submit 1 0 4 0 60','submit 1 1 0 0 60',
              'submit 1 1 4 2 60','submit 1 1 4 0 128','dispatch',
              'finish 1 0 1 1 4 0 1','finish 1 0 0 0 4 0 1','finish 1 0 0 1 3 0 1',
              'finish 1 0 0 3 0 0 1','finish 1 0 0 2 5 0 1','take 1 0 0',
              'finish 1 0 0 2 2 0 1','take 1 0 0','take 1 0 0']
    rows=replay(native,commands,policy)
    assert [r['status'] for r in rows[3:7]]==[1,2,3,2]
    assert not any(r['status'] for r in rows[8:14])
    assert rows[-2]['completion'][0:2]==[2,2] and not rows[-1]['status']


@pytest.mark.parametrize('policy',CONFIG['policies'])
def test_record_generation_exhaustion_never_reuses_old_token(policy,native):
    count=255 if policy=='dedicated' else 510
    commands=['open 0']
    for n in range(count):
        index=0 if n<255 else 1;generation=n%255+1
        commands+=['submit 0 0 1 0 60','dispatch',f'finish {generation} {index} 0 1 1 0 1',f'take {generation} {index} 0']
    commands+=['submit 0 0 1 0 60','ack 1 0 0','take 1 0 0','end 0','open 0','submit 0 0 1 0 60']
    rows=replay(native,commands,policy,short=True)
    assert rows[-6]['status']==rows[-1]['status']==5
    assert not rows[-5]['status'] and not rows[-4]['status']


@pytest.mark.parametrize('policy',CONFIG['policies'])
@pytest.mark.parametrize('seed',CONFIG['seeds'])
def test_generated_requests_stop_witnesses_and_consumer_pauses(policy,seed,native):
    rng=random.Random(0xc0ff+seed);model=reference.Model(policy=='dedicated');commands=[]
    def emit(command):commands.append(command);return model.apply(command)
    for n in range(CONFIG['operations']):
        if n%50==0:
            for i,r in enumerate(model.records):
                if r['phase']==2:emit(f'finish {r["generation"]} {i} {r["client"]} 1 {r["length"]} 20 1')
                elif r['phase']==1:emit(f'cancel {r["generation"]} {i} {r["client"]}')
                if r['phase']==3:emit(f'take {r["generation"]} {i} {r["client"]}')
            for c in range(3):emit(f'open {c}')
        op=rng.randrange(12);c=rng.randrange(4);i=rng.randrange(model.count+1)
        r=model.records[i] if i<model.count else None
        t=f'{r["generation"] if r else 0} {i} {r["client"] if r else c}'
        if op==0:emit(f'open {c}')
        elif op==1:emit(f'end {c}')
        elif op==2:emit(f'submit {c} {rng.randrange(3)} {rng.randrange(35)} {rng.randrange(3)} {rng.randrange(129)}')
        elif op==3:emit('dispatch')
        elif op==4:emit(f'finish {t} {rng.randrange(5)} {rng.randrange(35)} {rng.randrange(256)} {rng.randrange(2)}')
        elif op in (5,6,7,8):emit(f'{["cancel","notice","ack","take"][op-5]} {t}')
        elif op==9:emit(f'write {rng.randrange(3)} {rng.randrange(34)} {rng.randrange(256)}')
        elif op==10:emit(f'read {rng.randrange(3)} {rng.randrange(34)}')
        else:emit('snapshot')
    for c in range(3):emit(f'end {c}')
    for i,r in enumerate(model.records):
        if r['phase']==2:emit(f'finish {r["generation"]} {i} {r["client"]} 3 0 0 1')
        if r['phase']==3:emit(f'take {r["generation"]} {i} {r["client"]}')
    rows=replay(native,commands,policy)
    assert all(r[0]==0 for r in rows[-1]['records'])
    assert all(-1 not in buffer for buffer in rows[-1]['buffers'])
