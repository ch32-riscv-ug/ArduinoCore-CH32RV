"""N06 serialized Fabric/normal executor pipeline, not H417 hardware proof."""
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
    directory=tmp_path_factory.mktemp('h417-executor');source=(HERE/'h417_features_driver.cpp').read_text()
    observer=source[source.index('struct Observer {'):source.index('template<unsigned Features, Owners P, class Counter>')]
    (directory/'h417_executor_observer.inc').write_text(observer)
    command=[compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(directory),str(HERE/'h417_executor_driver.cpp'),'-o',str(directory/'app')]
    r=subprocess.run(command,capture_output=True,text=True,timeout=300)
    if destination:=os.environ.get('EXP06_H417_EXECUTOR_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),REPO/'planning/core-redesign/h417-executor-inputs.json']
        paths += [HERE/n for n in ['h417_executor.hpp','h417_executor_driver.cpp','h417_features.hpp','h417_features_driver.cpp','work_executor.hpp','event_executor.hpp','data_parts.hpp']]
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        for path in paths:
            target=out/'inputs'/path.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        shutil.copy2(directory/'h417_executor_observer.inc',out/'h417_executor_observer.inc')
        report=dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),compiler_sha256=sha(compiler),
                    observer_sha256=sha(directory/'h417_executor_observer.inc'),inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if not r.returncode:shutil.copy2(directory/'app',out/'app');report['executable_sha256']=sha(directory/'app')
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==0,r.stderr
    return directory/'app'


def run(native,commands,candidate=1,policy=1,features=3,short=False,safe=True,domain=0,opt=False):
    r=subprocess.run([str(native),str(candidate),str(policy),str(features),str(int(short)),str(int(safe)),str(domain),str(int(opt))],
                     input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    previous=None
    for command,row in zip(commands,rows):
        if previous is not None:
            for before,after in zip(previous['workers'],row['workers']):
                if command.startswith('turn '):assert 0<=after[7]-before[7]<=8
                else:assert after[1:]==before[1:]
        assert row['session'][0]==row['state'][1] and row['session'][1]==row['state'][3]
        previous=row
    if destination:=os.environ.get('EXP06_H417_EXECUTOR_EVIDENCE'):
        document=dict(candidate=candidate,policy=policy,features=features,short=short,safe=safe,domain=domain,opt=opt,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(document,sort_keys=True).encode()).hexdigest()
        Path(destination,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(document,separators=(',',':')).encode(),mtime=0))
    return rows


def compare(native,commands,**kwargs):
    rows=[run(native,commands,c,**kwargs) for c in range(1,5)]
    assert all(r==rows[0] for r in rows[1:]);return rows[0]


def turn(core=1,where=0,profile=0,context=0):return f'turn {core} {where} {profile} {context}'
def hash_values(values,initial=2166136261):
    for value in values:initial=((initial^value)*16777619)&0xffffffff
    return initial


@pytest.mark.parametrize('policy',[0,1,2])
@pytest.mark.parametrize('features',[0,1,2,3])
def test_feature_projection_publish_consume_and_release(native,policy,features):
    consumer=0 if policy==0 else 1
    commands=[f'push 0 {consumer} 10','notify','reserve 0 0','fill 0 0 20',f'publish 0 0 {consumer}','notify']+[turn(consumer)]*32
    rows=compare(native,commands,policy=policy,features=features)
    assert rows[0]['status']==int(bool(features&1)) and rows[2]['status']==int(bool(features&2))
    w=rows[-1]['workers'][consumer]
    assert w[3]==int(bool(features&1)) and w[8]==int(bool(features&2))
    if features&1:assert w[4]==hash_values([10,11,12,13])
    if features&2:
        assert w[7]==64 and not w[5] and w[9]==hash_values([hash_values(range(20,84))])
        assert rows[-1]['blocks'][0][0]==0
    assert rows[-1]['session'][2:4]==[0,0]


@pytest.mark.parametrize('features',[1,2,3])
def test_source_data_is_separate_from_explicit_ticket_delivery(native,features):
    if features==1:commands=['push 0 1 10']
    else:commands=['reserve 0 0','fill 0 0 20','publish 0 0 1']
    commands += [turn()]*12+['notify']+[turn()]*24
    rows=compare(native,commands,features=features)
    before=rows[len(commands)-25];assert before['workers'][1][1]==0
    assert rows[-1]['workers'][1][3 if features==1 else 8]==1


@pytest.mark.parametrize('features',[1,2,3])
def test_busy_probe_retains_work_for_explicit_retry(native,features):
    prefix=['push 0 1 10','notify'] if features==1 else ['reserve 0 0','fill 0 0 20','publish 0 0 1','notify']
    commands=prefix+['gate 1',turn(),'gate 0']+[turn()]*24
    rows=compare(native,commands,features=features)
    blocked=rows[len(prefix)+1];assert blocked['workers'][1][2]==1 and blocked['session'][3]
    assert rows[-1]['workers'][1][3 if features==1 else 8]==1


@pytest.mark.parametrize('blocked_at',[0,1,5,9])
def test_block_take_read_release_each_retry_without_losing_lease(native,blocked_at):
    prefix=['reserve 0 0','fill 0 0 20','publish 0 0 1','notify']+[turn()]*blocked_at
    commands=prefix+['gate 1',turn(),'gate 0']+[turn()]*20
    rows=compare(native,commands,features=2)
    assert rows[len(prefix)+1]['workers'][1][2]==1
    assert rows[-1]['workers'][1][7:10]==[64,1,hash_values([hash_values(range(20,84))])]


def test_clear_before_probe_preserves_arrival_after_empty_negative_control(native):
    commands=['hint 1 1 1','inject 0 40',turn(),turn(),turn()]
    good=compare(native,commands,features=1,safe=True)
    bad=compare(native,commands,features=1,safe=False)
    assert good[2]['session'][3]==1 and good[-1]['workers'][1][3]==1
    assert bad[2]['session'][3]==0 and bad[-1]['workers'][1][3]==0 and bad[-1]['state'][6]==1
    repaired=compare(native,commands+['hint 1 1 1',turn()],features=1,safe=False)
    assert repaired[-1]['workers'][1][3]==1


@pytest.mark.parametrize('policy',[0,1,2])
def test_stopped_consumer_retains_capacity_and_explicit_resumes(native,policy):
    core=0 if policy==0 else 1
    commands=[f'online {core} 0']
    commands += sum(([f'push 0 {core} {i}','notify'] for i in range(5)),[])
    commands += [turn(core),f'online {core} 1']+[turn(core)]*8
    rows=compare(native,commands,policy=policy,features=1)
    assert [r['status'] for cmd,r in zip(commands,rows) if cmd.startswith('push')]==[1,1,1,1,0]
    assert rows[11]['status']==1
    assert rows[-1]['workers'][core][3]==4
    assert rows[-1]['workers'][core][4]==hash_values([v for i in range(4) for v in range(i,i+4)])


@pytest.mark.parametrize('at',[0,1,5,9,10])
def test_recovery_stale_ticket_and_lease_cannot_release_new_generation(native,at):
    prefix=['reserve 0 0','fill 0 0 20','publish 0 0 1','notify']+[turn()]*at
    commands=prefix+['recover','attest 7','restart','online 0 0','online 1 0','attest 3','restart','dma 1','attest 4','restart',
                     'notify','publish 0 0 1','online 0 1','online 1 1','reserve 0 1','fill 0 1 90','publish 0 1 1','notify']+[turn()]*24
    rows=compare(native,commands,features=2)
    k=len(prefix);assert rows[k+1]['status']==rows[k+2]['status']==0
    assert rows[k+6]['status']==0 and rows[k+9]['status']==1
    assert rows[k+10]['status']==rows[k+11]['status']==0
    assert rows[-1]['session'][0]==2 and rows[-1]['workers'][1][8]==(2 if at==10 else 1)
    assert rows[-1]['workers'][1][10]==int(0<at<10)
    assert rows[-1]['blocks'][0][0]==0


@pytest.mark.parametrize('where',[1,2])
@pytest.mark.parametrize('profile',[0,1,2])
@pytest.mark.parametrize('domain,opt',[(0,False),(1,False),(1,True)])
def test_opportunity_policy_applies_to_message_delivery(native,where,profile,domain,opt):
    commands=['push 0 1 10','notify',turn(1,where,profile),turn()]
    rows=compare(native,commands,features=1,domain=domain,opt=opt)
    allowed=profile!=0 and (domain==0 or (profile==2 and opt))
    assert rows[2]['workers'][1][3]==int(allowed)
    assert rows[-1]['workers'][1][3]==1


@pytest.mark.parametrize('context,expected',[(1,2),(2,3),(3,2)])
def test_forbidden_context_does_not_pop_or_read(native,context,expected):
    commands=['push 0 1 10','notify',turn(1,0,0,context),turn()]
    rows=compare(native,commands,features=1)
    assert rows[2]['status']==expected and rows[2]['workers'][1][3]==0
    assert rows[-1]['workers'][1][3]==1


def test_two_consumers_shared_owner_and_round_robin_lanes(native):
    commands=['reserve 0 0','fill 0 0 20','publish 0 0 1','notify','hint 1 2 0',turn(0),turn(1)]
    for i in range(10):commands += [f'push 0 0 {i}','notify',turn(0),turn(0)]
    commands += [turn(0)]*16
    rows=compare(native,commands,policy=2)
    assert rows[5]['workers'][0][5] and not rows[6]['workers'][1][5]
    assert rows[-1]['workers'][0][3]==10 and rows[-1]['workers'][0][8]==1
    assert rows[-1]['workers'][0][7]==64 and rows[-1]['workers'][1][7]==0


def test_small_epoch_exhaustion_rejects_wrap(native):
    commands=[]
    for _ in range(255):commands+=['recover','online 0 0','online 1 0','dma 1','attest 7','restart','online 0 1','online 1 1']
    rows=compare(native,commands,features=0,short=True)
    restarts=[r for c,r in zip(commands,rows) if c=='restart']
    assert all(r['status']==1 for r in restarts[:-1]) and restarts[-1]['status']==0
    assert rows[-1]['session'][0:2]==[255,1]


@pytest.mark.parametrize('seed',range(10))
def test_generated_pipeline_payload_oracle_and_restarts(native,seed):
    rng=random.Random(seed);commands=[];expected=[];count=0;mh=2166136261;blocks=0;bh=2166136261;epoch=1
    for cycle in range(100):
        if rng.randrange(8)==0:
            commands+=['recover','online 0 0','online 1 0','dma 1','attest 7','restart','online 0 1','online 1 1'];epoch+=1
        amount=rng.randrange(1,5);payloads=[rng.randrange(1000) for _ in range(amount)]
        for value in payloads:commands += [f'push 0 1 {value}','notify'];mh=hash_values(range(value,value+4),mh);count+=1
        value=rng.randrange(160)
        commands += ['reserve 0 0',f'fill 0 0 {value}','publish 0 0 1','notify']
        commands += [turn()]*32
        blocks+=1;bh=hash_values([hash_values(range(value,value+64))],bh)
        expected.append((len(commands)-1,count,mh,blocks,bh,epoch))
    rows=compare(native,commands)
    for index,count,mh,blocks,bh,epoch in expected:
        row=rows[index];w=row['workers'][1]
        assert w[3:5]==[count,mh] and w[8:10]==[blocks,bh]
        assert row['session'][0]==epoch and row['session'][2:4]==[0,0]
        assert not w[5] and all(b[0]==0 for b in row['blocks'])


@pytest.mark.parametrize('at',[1,5,9])
def test_stopped_reader_holds_pool_capacity_until_normal_release(native,at):
    commands=['reserve 0 0','fill 0 0 20','publish 0 0 1','notify',
              'reserve 0 1','fill 0 1 90','publish 0 1 1','notify']+[turn()]*at
    commands+=['online 1 0','reserve 0 0',turn(),'online 1 1']+[turn()]*32+['reserve 0 0']
    rows=compare(native,commands,features=2)
    k=8+at
    assert rows[k]['workers'][1][5] and rows[k+1]['status']==0
    assert rows[k+2]['workers'][1][7]==rows[k]['workers'][1][7]
    assert rows[-2]['workers'][1][7:9]==[128,2] and rows[-1]['status']==1
    assert rows[-1]['slots'][0][2]==3


def test_failed_publication_does_not_replace_previous_ticket(native):
    commands=['push 0 1 10','gate 1','push 0 1 40','notify',turn(),'gate 0']+[turn()]*4
    rows=compare(native,commands,features=1)
    assert rows[2]['status']==0 and rows[2]['ticket']==rows[0]['ticket']
    assert rows[-1]['workers'][1][3:5]==[1,hash_values(range(10,14))]


def test_small_block_generation_exhaustion_does_not_wrap_after_restart(native):
    commands=[]
    for _ in range(255):commands+=['reserve 0 0','fill 0 0 20','publish 0 0 1','notify']+[turn()]*11
    commands+=['reserve 0 0','recover','online 0 0','online 1 0','dma 1','attest 7','restart','online 0 1','online 1 1','reserve 0 0']
    rows=compare(native,commands,features=2,short=True)
    reserves=[r for c,r in zip(commands,rows) if c=='reserve 0 0']
    assert all(r['status']==1 for r in reserves[:255])
    assert reserves[-2]['status']==reserves[-1]['status']==0
    assert rows[-1]['workers'][1][8]==255 and rows[-1]['slots'][0][2]==255
    assert rows[-1]['session'][0]==2
