"""Conditional N03 register adapter; host model assumptions are not silicon proof."""
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

HERE=Path(__file__).parent
REPO=HERE.parents[2]
spec=importlib.util.spec_from_file_location('slave_reference',HERE/'test_k0_candidates.py')
reference=importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    dest=tmp_path_factory.mktemp('k0-slave')/'app'
    command=[compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(HERE/'k0_slave_register_driver.cpp'),'-o',str(dest)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=240)
    if directory:=os.environ.get('EXP06_K0_SLAVE_EVIDENCE'):
        out=Path(directory);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),*[HERE/n for n in ['k0_slave_register.hpp','k0_slave_register_driver.cpp','k0_shared.hpp',
               'k0_candidates.hpp','data_parts.hpp','work_executor.hpp','event_executor.hpp','test_k0_candidates.py','oracle.py']],
               REPO/'planning/core-redesign/k0-slave-register-inputs.json']
        for path in paths:
            target=out/'inputs'/path.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        report=dict(command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr,
                    compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),
                    inputs={str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
        if result.returncode==0:
            shutil.copy2(dest,out/'app');report['executable_sha256']=hashlib.sha256(dest.read_bytes()).hexdigest()
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert result.returncode==0,result.stderr
    return dest


def run(native,commands,candidate,capacity=1,policy=0,nacked_byte=False,foreign_stop=False):
    r=subprocess.run([str(native),str(candidate),str(capacity),str(policy),str(int(nacked_byte)),str(int(foreign_stop))],
                     input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    if directory:=os.environ.get('EXP06_K0_SLAVE_EVIDENCE'):
        document=dict(candidate=candidate,capacity=capacity,policy=policy,nacked_byte=nacked_byte,
                      foreign_stop=foreign_stop,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(document,sort_keys=True).encode()).hexdigest()
        Path(directory,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(document,separators=(',',':')).encode(),mtime=0))
    return rows


def frame(seq,step=1,lo=-1000,hi=1000,length=8,pair=False):
    data=list(struct.pack('<Hhhh',seq,step,lo,hi))+[77]
    cmds=['write','irq']
    for i,value in enumerate(data[:length]):
        cmds.append(f'byte {value}')
        if not pair or i%2==1:cmds.append('irq')
    return cmds+['stop','irq']


def read(length):
    return ['read','irq','irq']+[c for i in range(length) for c in [f'consume {int(i==length-1)}','irq']]+['stop','irq']


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('policy',[0,1])
@pytest.mark.parametrize('length',[0,7,8,9])
def test_frame_length_and_tail(native,candidate,policy,length):
    commands=frame(23,2,length=length,pair=True)+['step','input 3','step']
    rows=run(native,commands,candidate,policy=policy)
    assert rows[-1]['meaning']==([6,2,-1000,1000,23,0,0,0] if length==8 else [3,1,-1000,1000,0,4,0,0])
    stop=commands.index('stop'); before=rows[stop]; after=rows[stop+1]
    if length%2:assert before['port'][6]==1
    assert after['port'][6]==0
    log=after['log'];assert log.count('CR-ACK1')==1
    if length%2:assert log.index('DR-read')<log.index('CR-ACK1')


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('length',[1,7,8,9,12])
def test_prefetch_snapshot_and_normal_nack(native,candidate,length):
    commands=['input 255','step','read','irq','irq','consume 0','irq','input 1','step']
    commands += [c for i in range(1,length) for c in [f'consume {int(i==length-1)}','irq']]
    if length==1: # Terminate after the already consumed first byte, without another byte.
        commands=['input 255','step','read','irq','irq','consume 1','irq','input 1','step']
    commands+=['stop','irq']+read(8)
    rows=run(native,commands,candidate)
    consumed=[row['port'][5] for command,row in zip(commands,rows) if command.startswith('consume')]
    assert consumed[:length]==([255]+[0]*7+[0]*max(0,length-8))[:length]
    assert consumed[length:]==[0,1,0,0,0,0,0,0]
    assert rows[-1]['meaning']==[256,1,-1000,1000,0,0,0,0]
    release=rows[commands.index('read')+1]
    assert release['release_snapshot']==[255,0,0,0,0,0,0,0]
    assert release['port'][7]==1 and rows[commands.index('read')+2]['port'][7]==2
    assert release['frontend'][3]==1 # DR prefetch != number of peer-consumed bytes.


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('capacity',[1,2])
@pytest.mark.parametrize('policy',[0,1])
def test_full_reject_read_and_explicit_rearm(native,candidate,capacity,policy):
    initial=sum([frame(i+1) for i in range(capacity)],[])
    rejected=['write','irq','byte 9','irq','stop','irq']
    commands=initial+rejected+['read']
    rows=run(native,commands,candidate,capacity,policy)
    at=rows[len(initial)+1]
    assert at['meaning'][5:]==[2,0,capacity]
    assert at['frontend'][4]==0
    assert rows[len(initial)+2]['port'][4]==int(policy==0)
    assert rows[-1]['port'][4]==int(policy==0)
    if policy==1:
        # The read address was NACKed too; no new ADDR IRQ occurs in this model.
        assert rows[-1]['port'][2]&2==0
        commands=initial+rejected+['rearm','read','irq','irq','consume 1','irq','stop','irq']+['step']*capacity+frame(99,3)+['step']
        rows=run(native,commands,candidate,capacity,policy)
        rearm=commands.index('rearm');assert rows[rearm]['port'][8]==1
    else:
        commands=initial+rejected+read(1)+['step']*capacity+frame(99,3)+['step']
        rows=run(native,commands,candidate,capacity,policy)
    assert rows[-1]['meaning']==[0,3,-1000,1000,99,2,0,0]
    # A read while completed FIFO is full must not itself set rejection.
    fresh=run(native,initial+read(1),candidate,capacity,policy)
    assert fresh[-1]['meaning'][5]==0


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('nacked_byte',[False,True])
def test_rearm_does_not_run_on_busy_or_deliver_rejected_data(native,candidate,nacked_byte):
    commands=frame(1)+['write','irq','byte 55','irq','rearm','step','rearm','stop','irq','rearm']+frame(2)+['step']
    rows=run(native,commands,candidate,policy=1,nacked_byte=nacked_byte)
    indices=[i for i,c in enumerate(commands) if c=='rearm']
    assert [rows[i]['port'][8] for i in indices]==[0,0,1]
    assert rows[-1]['meaning']==[0,1,-1000,1000,2,2,0,0]


@pytest.mark.parametrize('candidate',range(1,5))
def test_repeated_own_address_aborts_even_complete_staging(native,candidate):
    commands=frame(11)[:-2]+read(8)+['step']
    rows=run(native,commands,candidate)
    assert rows[-1]['meaning']==[0,1,-1000,1000,0,4,0,0]
    assert [r['port'][5] for c,r in zip(commands,rows) if c.startswith('consume')]==[0]*8


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('error',[256,512,2048,256|2048])
def test_error_disables_port_until_explicit_recovery(native,candidate,error):
    commands=frame(9)[:-2]+[f'error {error}','irq','step','recover','stop','recover']+frame(10,2)+['step']
    rows=run(native,commands,candidate)
    idx=commands.index(f'error {error}')+1
    assert rows[idx]['port'][0]==4 and rows[idx]['port'][1]&1025==0
    assert rows[idx]['meaning'][7]==0
    recovery=[r['port'][8] for c,r in zip(commands,rows) if c=='recover']
    assert recovery==[0,1]
    assert rows[-1]['meaning']==[0,2,-1000,1000,10,4,0,0]


@pytest.mark.parametrize('candidate',range(1,5))
def test_old_af_and_new_address_are_separate(native,candidate):
    commands=['input 5','step','read','irq','consume 1','read','irq','irq','consume 1','irq']
    rows=run(native,commands,candidate)
    assert [r['port'][5] for c,r in zip(commands,rows) if c.startswith('consume')]==[5,5]
    assert rows[-1]['meaning'][5]==0


def test_sdk_event_read_releases_address_before_adapter(native):
    commands=['input 55','step','read','sdk_event','irq']
    rows=run(native,commands,1)
    assert rows[3]['release_snapshot']==[0]*8
    assert rows[3]['port'][9]==1
    assert rows[-1]['port'][7]==0 # ADDR was consumed; adapter did not start TX.


@pytest.mark.parametrize('foreign_stop',[False,True])
def test_foreign_restart_is_outside_proven_wire_profile(native,foreign_stop):
    commands=frame(17)[:-2]+['other','irq','stop','irq','step']
    rows=run(native,commands,1,foreign_stop=foreign_stop)
    # Two explicitly assumed STOPF models; neither observes the foreign START.
    # With a STOPF it wrongly commits; without one the staged write is retained.
    assert rows[-1]['meaning'][4]==(17 if foreign_stop else 0)
    assert rows[-1]['frontend'][0]==int(not foreign_stop)
    assert rows[-1]['meaning'][5]==0 # Neither model records required invalid status.


@pytest.mark.parametrize('seed',range(10))
def test_serial_meaning_against_independent_oracle(native,seed):
    rng=random.Random(seed);commands=[];semantic=[];checkpoints=[]
    model=reference.reference.KnobOracle()
    for _ in range(1000):
        if rng.randrange(3)==0:
            seq=rng.randrange(65536);step=rng.choice([0,1,2,127]);lo,hi=rng.choice([(-1000,1000),(-20,50),(10,-10)])
            commands+=frame(seq,step,lo,hi,pair=bool(rng.randrange(2)))
            semantic=['begin_write',*[f'byte {b}' for b in struct.pack('<Hhhh',seq,step,lo,hi)],'stop']
        else:
            delta=rng.randrange(-100,101);commands += [f'input {delta}','step'];semantic=[f'input {delta}','step']
        for c in semantic:model.apply(c)
        checkpoints.append((len(commands)-1,model.apply('observe')[:8]))
    commands += ['step']
    model.apply('step');checkpoints.append((len(commands)-1,model.apply('observe')[:8]))
    for candidate in range(1,5):
        rows=run(native,commands,candidate)
        for index,expected in checkpoints:assert rows[index]['meaning']==expected,(seed,candidate,index)


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('policy',[0,1])
def test_admission_before_release_and_final_byte_before_new_address(native,candidate,policy):
    commands=frame(1)+['write','irq']
    rows=run(native,commands,candidate,policy=policy)
    at=rows[-1]
    assert bool(at['port'][10]&1024)==(policy==0)
    if policy==1:assert at['log'].index('CR-ACK0')<at['log'].index('SR2-release')
    # Last old byte is still in DR when a repeated own read is addressed.
    commands=frame(19)[:-2]
    # Construct an eight-byte staging with its last byte not drained yet.
    commands=commands[:-1]+['read','irq','irq','consume 1','irq','step']
    rows=run(native,commands,candidate,policy=policy)
    assert rows[-1]['meaning']==[0,1,-1000,1000,0,4,0,0]
    at=rows[commands.index('read')+1]
    assert at['log'].index('DR-read')<at['log'].index('SR2-release')


@pytest.mark.parametrize('candidate',range(1,5))
def test_stop_and_next_address_with_tail_preserves_completed_frame(native,candidate):
    # Address of the new write arrives after STOP while its IRQ is delayed.
    commands=frame(21)[:-3]+['stop','write','irq','stop','irq','step']
    rows=run(native,commands,candidate)
    at=rows[commands.index('write',1)+1]
    assert at['meaning'][7]==1 and at['frontend'][4]==0
    assert rows[-1]['meaning']==[0,1,-1000,1000,21,2,0,0]


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('profile',[0,1,2])
def test_irq_does_not_execute_main_and_progress_respects_profile(native,candidate,profile):
    commands=frame(27,2)+['input 3',f'opportunity 1 {profile} 0']
    rows=run(native,commands,candidate)
    assert rows[-2]['meaning']==[0,1,-1000,1000,0,0,3,1]
    assert rows[-1]['meaning']==([0,1,-1000,1000,0,0,3,1] if profile==0 else [6,2,-1000,1000,27,0,0,0])
