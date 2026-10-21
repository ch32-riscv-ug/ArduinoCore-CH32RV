"""N06 executor/lease/clock/sleep composition; serial model, no MCU sleep."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

HERE=Path(__file__).parent
REPO=HERE.parents[2]


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    destination=tmp_path_factory.mktemp('sleep-work')/'app'
    command=[compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(HERE/'sleep_work_driver.cpp'),'-o',str(destination)]
    r=subprocess.run(command,capture_output=True,text=True,timeout=300)
    if directory:=os.environ.get('EXP06_SLEEP_WORK_EVIDENCE'):
        out=Path(directory);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),REPO/'planning/core-redesign/sleep-work-inputs.json']
        paths += [HERE/n for n in ['sleep_work.hpp','sleep_work_driver.cpp','sleep_coordinator.hpp','io_lifecycle.hpp','logical_time.hpp','deadline_timer.hpp','event_executor.hpp','work_executor.hpp','data_parts.hpp']]
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        for path in paths:
            target=out/'inputs'/path.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        report=dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),compiler_sha256=sha(compiler),
                    inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if not r.returncode:shutil.copy2(destination,out/'app');report['executable_sha256']=sha(destination)
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==0,r.stderr
    return destination


def run(native,commands,candidate=1,capacity=0,timed=True,safe=True,domain=0,opt=False):
    r=subprocess.run([str(native),str(candidate),str(capacity),str(int(timed)),str(int(safe)),str(domain),str(int(opt))],
                     input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    previous=None
    for command,row in zip(commands,rows):
        for w in row['workers']:
            assert w[3]==1-w[6] # live I/O always owns the buffer
            if row['phase']==4:
                assert w[:4]==[0,0,0,0]
                if safe:assert w[8]==0
        if row['phase']==4:assert row['clock'][0]==2 and not row['clock_ready'] and row['frozen']
        if previous is not None and not command.startswith('turn '):
            # No ordinary callbacks inside coordinator/clock/timer operations.
            assert [w[9:12] for w in row['workers']]==[w[9:12] for w in previous['workers']]
        previous=row
    if directory:=os.environ.get('EXP06_SLEEP_WORK_EVIDENCE'):
        document=dict(candidate=candidate,capacity=capacity,timed=timed,safe=safe,domain=domain,opt=opt,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(document,sort_keys=True).encode()).hexdigest()
        Path(directory,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(document,separators=(',',':')).encode(),mtime=0))
    return rows


def compare(native,commands,capacity=0,**kwargs):
    rows=[run(native,commands,c,capacity,**kwargs) for c in range(1,5)]
    assert all(r==rows[0] for r in rows[1:]);return rows[0]


def turn(side=0,where=0,profile=0,context=0):return f'turn {side} {where} {profile} {context}'


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('timed',[False,True])
@pytest.mark.parametrize('side',[0,1])
def test_jobs_drain_before_armed_and_off_without_hidden_calls(native,capacity,timed,side):
    commands=[f'job {side} 3','request','poll',f'job {side} 1',f'start {side} 1',turn(side),'poll',turn(side),'poll',turn(side),'poll','enter 1 1 1 0',turn(side)]
    rows=compare(native,commands,capacity,timed=timed)
    assert rows[0]['status']==1 and rows[1]['phase']==2 and rows[2]['phase']==2
    assert rows[3]['status']==0 and rows[4]['status']==4
    assert [rows[i]['workers'][side][8] for i in [5,7,9]]==[2,1,0]
    assert rows[10]['phase']==3 and rows[11]['phase']==4 and rows[12]['status']==1
    assert rows[11]['workers'][side][9:12]==[3,0,3]


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('timed',[False,True])
@pytest.mark.parametrize('outcome',[1,2,3])
def test_cancellation_witness_then_worker_collection_then_jobs(native,capacity,timed,outcome):
    commands=['start 0 4','job 0 2','request','poll','finish 0 1 3 2 77 0','finish 0 2 3 2 77 1',
              f'finish 0 1 {outcome} {4 if outcome==1 else 2} 77 1','poll',turn(),'poll',turn(),'poll',turn(),'poll','enter 1 1 1 0']
    rows=compare(native,commands,capacity,timed=timed)
    assert rows[2]['workers'][0][3:7]==[1,0,1,0]
    assert rows[4]['status']==rows[5]['status']==0
    assert rows[7]['workers'][0][3:7]==[1,1,1,0] and rows[7]['phase']==2
    assert rows[8]['workers'][0][3:7]==[0,0,0,1]
    assert rows[8]['workers'][0][8:14]==[2,0,1,1,outcome,4 if outcome==1 else 2]
    assert rows[-2]['phase']==3 and rows[-1]['phase']==4


@pytest.mark.parametrize('where',[1,2])
@pytest.mark.parametrize('profile',[0,1,2])
@pytest.mark.parametrize('domain,opt',[(0,False),(1,False),(1,True)])
def test_implicit_opportunity_policy_controls_sleep_progress(native,where,profile,domain,opt):
    allowed=profile!=0 and (domain==0 or (profile==2 and opt))
    commands=['job 0 1','request',turn(0,where,profile),'poll',turn(),'poll']
    for capacity in [0,2,4]:
        rows=compare(native,commands,capacity,domain=domain,opt=opt)
        assert rows[2]['status']==(4 if profile==0 else 0)
        assert rows[2]['workers'][0][8]==int(not allowed)
        assert rows[3]['phase']==(3 if allowed else 2)
        assert rows[-1]['phase']==3


@pytest.mark.parametrize('context,expected',[(1,2),(2,3),(3,2)])
def test_forbidden_contexts_cannot_collect_or_drain(native,context,expected):
    commands=['job 0 1','start 0 4','request','finish 0 1 3 2 77 1',turn(0,0,0,context),'poll',turn(),turn(),'poll']
    for capacity in [0,2,4]:
        rows=compare(native,commands,capacity)
        assert rows[4]['status']==expected and rows[4]['workers'][0][3:5]==[1,1]
        assert rows[4]['workers'][0][8:12]==[1,0,0,0]
        assert rows[-1]['phase']==3


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('known',[False,True])
@pytest.mark.parametrize('timed',[False,True])
def test_restore_requires_clock_then_all_ports_before_admission(native,capacity,known,timed):
    commands=['observe 1 60000000 60000000','request','poll','enter 1 1 1 0','wake 2','wake 1',
              'ready 0 1','poll',turn(),'job 0 1',f'restore 1 2 0 {int(known)} 2500000 0','poll','start 0 4','ready 1 1','poll','job 0 1',turn()]
    rows=compare(native,commands,capacity,timed=timed)
    assert rows[4]['status']==0 and rows[5]['phase']==5
    assert rows[7]['workers'][0][:2]==[0,0] and rows[8]['status']==1
    assert rows[9]['status']==0
    assert rows[10]['stamp']==([12500000,1,0] if known else [0,2,0])
    assert rows[11]['workers'][0][:2]==[1,0] and rows[11]['workers'][1][:2]==[0,0]
    assert rows[12]['status']==4 and rows[14]['phase']==1 and rows[15]['status']==1
    assert rows[-1]['workers'][0][9]==1


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('side',[0,1])
def test_partial_restore_fault_retry_does_not_admit_or_drop_work(native,capacity,side):
    commands=['request','poll','enter 1 1 1 0','wake 1','restore 1 2 0 0 0 0',f'ready {side} 2',f'ready {1-side} 1','poll',
              'job 0 1','start 1 4','retry',f'ready {side} 1','poll','job 0 1',turn()]
    rows=compare(native,commands,capacity)
    assert rows[7]['phase']==6 and rows[7]['frozen']
    assert all(w[1]==0 for w in rows[7]['workers'])
    assert rows[8]['status']==0 and rows[9]['status']==4
    assert rows[12]['phase']==1 and not rows[12]['frozen']
    assert rows[-1]['workers'][0][9]==1


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('injection',[1,2,3])
def test_input_wake_boundaries_keep_jobs_and_resume_explicitly(native,capacity,injection):
    commands=['job 0 1',turn(),'request','poll',f'enter 1 1 1 {injection}','poll','take_input']
    if injection!=1:commands+=['restore 1 2 0 0 0 0','ready 0 1','ready 1 1','poll']
    commands+=['job 0 2',turn(),turn()]
    rows=compare(native,commands,capacity)
    assert rows[4]['phase']==(1 if injection==1 else 5)
    assert rows[6]['status']==1 and rows[-1]['phase']==1
    assert rows[-1]['workers'][0][9:12]==[3,0,3]


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('known',[False,True])
def test_timer_pending_and_domain_change_alongside_worker(native,capacity,known):
    commands=['arm 1000000 1000000','job 0 1',turn(),'request','poll','enter 1 1 1 0','wake 1',
              f'restore 1 2 0 {int(known)} 2500000 0','timer_poll','ready 0 1','ready 1 1','poll','request','timer_take','request']
    rows=compare(native,commands,capacity)
    assert rows[8]['status']==(0 if known else 5)
    assert rows[12]['status']==(0 if known else 1)
    assert rows[13]['status']==int(known)
    if known:assert rows[13]['notice']==[2000000,1,1] and rows[-1]['status']==1
    else:assert rows[8]['timer'][:2]==[0,0] and rows[-1]['status']==0 # already quiescing


def test_ports_only_quiet_is_insufficient_negative_control(native):
    commands=['job 0 3','request','poll','enter 1 1 1 0']
    safe=compare(native,commands,safe=True)
    unsafe=compare(native,commands,safe=False)
    assert safe[-1]['phase']==2 and safe[-1]['status']==0
    assert unsafe[-1]['phase']==4 and unsafe[-1]['workers'][0][8]==3


@pytest.mark.parametrize('seed',range(10))
def test_generated_lifecycles_with_independent_step_accounting(native,seed):
    rng=random.Random(seed);commands=[];expected=[];completed=[0,0];collected=[0,0]
    for cycle in range(100):
        counts=[rng.randrange(1,5),rng.randrange(1,5)];order=[0,1];rng.shuffle(order)
        for i in order:commands+=[f'job {i} {counts[i]}',f'start {i} 4']
        commands+=['request','poll']
        for i in order:
            commands+=[f'finish {i} {cycle+1} 3 2 77 1','poll',turn(i)]
            collected[i]+=1
            for _ in range(counts[i]):commands.append(turn(i));completed[i]+=1
            commands.append('poll')
        commands+=['poll',f'enter {cycle+1} 1 1 0',f'wake {cycle+1}',f'restore {cycle+1} {cycle+2} 0 0 0 0','ready 0 1','ready 1 1','poll']
        expected.append((len(commands)-1,completed.copy(),collected.copy()))
    rows=compare(native,commands,[0,2,4][seed%3],timed=bool(seed&1))
    for index,steps,takes in expected:
        row=rows[index];assert row['phase']==1 and not row['frozen']
        assert [w[9] for w in row['workers']]==steps
        assert [w[10] for w in row['workers']]==takes
        assert all(w[3:7]==[0,0,0,1] and w[8]==0 for w in row['workers'])


@pytest.mark.parametrize('capacity',[0,2,4])
@pytest.mark.parametrize('timed',[False,True])
def test_abort_with_live_io_keeps_cancel_and_pending_job(native,capacity,timed):
    commands=['job 0 2','start 0 4','request','post','poll','start 0 4','job 0 1',turn(),
              'finish 0 1 3 2 77 1',turn(),turn(),'finish 0 1 1 4 99 1','take_input','request','poll','enter 2 1 1 0']
    rows=compare(native,commands,capacity,timed=timed)
    assert rows[4]['phase']==1 and not rows[4]['frozen'] and rows[4]['input']
    assert rows[4]['workers'][0][1:6]==[1,1,1,0,1]
    assert rows[5]['status']==1 and rows[6]['status']==0
    assert rows[10]['workers'][0][8:12]==[0,2,1,3]
    assert rows[11]['status']==0 and rows[-1]['phase']==4


@pytest.mark.parametrize('capacity',[0,2,4])
def test_deadline_published_after_armed_aborts_entry(native,capacity):
    commands=['arm 1000 0','job 0 1','request',turn(),'poll','observe 1 6000 6000','timer_poll',
              'enter 1 1 1 0','timer_take','request']
    rows=compare(native,commands,capacity)
    assert rows[4]['phase']==3 and rows[6]['timer'][:2]==[0,1]
    assert rows[7]['status']==0 and rows[7]['phase']==1 and not rows[7]['frozen']
    assert rows[7]['workers'][0][9:12]==[1,0,1]
    assert rows[8]['notice']==[1000,0,1] and rows[9]['status']==1
