"""N04 artificial finite dependency-pump comparison; no RTOS or real-time claim."""
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
COMPLETED,PENDING,LIMIT,ACTIVE,BUSY,HELD,SCOPE,USER,FORBIDDEN,INVALID=range(10)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    app=tmp_path_factory.mktemp('depth')/'app'
    command=[compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(HERE/'reentry_depth_driver.cpp'),'-o',str(app)]
    r=subprocess.run(command,capture_output=True,text=True,timeout=180)
    if dest:=os.environ.get('EXP03_DEPTH_EVIDENCE'):
        out=Path(dest);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),*[HERE/n for n in ['reentry_depth.hpp','reentry_depth_driver.cpp','wait_policy.hpp','work_executor.hpp']],
               REPO/'planning/core-redesign/reentry-depth-inputs.json']
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        report=dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),
                    compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),
                    inputs={str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
        if not r.returncode:
            shutil.copy2(app,out/'app');report['executable_sha256']=hashlib.sha256(app.read_bytes()).hexdigest()
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==0,r.stderr
    return app


def run(native,commands,depth=1,nested=True):
    r=subprocess.run([str(native),str(depth),str(int(nested))],input='\n'.join(commands)+'\n',
                     capture_output=True,text=True,timeout=30)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(l) for l in r.stdout.splitlines()];assert len(rows)==len(commands)
    if dest:=os.environ.get('EXP03_DEPTH_EVIDENCE'):
        d=dict(depth=depth,nested=nested,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()
        Path(dest,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(d,separators=(',',':')).encode(),mtime=0))
    return rows


def chain(length):return [f'node {i} {i+1 if i+1<length else -1} -1 -1 1 0' for i in range(length)]


@pytest.mark.parametrize('depth',[1,2,4])
@pytest.mark.parametrize('length',range(1,6))
def test_chain_retains_frames_until_depth_limit(native,depth,length):
    rows=run(native,chain(length)+['run 0 0'],depth)
    last=rows[-1]
    assert last[:4]==([COMPLETED,(1<<length)-1,length,length] if length<=depth else [LIMIT,0,depth,depth])
    assert last[4]==int(length>depth)


@pytest.mark.parametrize('length',range(1,6))
def test_continuation_does_not_enter_producer_and_eventually_progresses(native,length):
    rows=run(native,chain(length)+['run 0 0']+['round']*5,nested=False)
    first=rows[length]
    assert first[0]==(COMPLETED if length==1 else PENDING)
    assert first[2:4]==[1,1]
    assert rows[-1][1]==31 and rows[-1][2]==1


@pytest.mark.parametrize('depth',[1,2,4])
@pytest.mark.parametrize('cycle_length',[1,2,3,5])
def test_cycles_do_not_become_timeout_success(native,depth,cycle_length):
    commands=chain(cycle_length)
    commands[-1]=f'node {cycle_length-1} 0 -1 -1 1 0'
    rows=run(native,commands+['run 0 0']+['round']*6,depth)
    first=rows[cycle_length]
    assert first[0]==(ACTIVE if cycle_length<=depth else LIMIT)
    assert rows[-1][1]&((1<<cycle_length)-1)==0
    assert rows[-1][2]<=depth


@pytest.mark.parametrize('depth',[1,2,4])
def test_active_ancestor_resource_cannot_be_pumped_away(native,depth):
    commands=['node 0 1 -1 0 1 0','node 1 -1 0 -1 1 0','run 0 0','observe','run 1 0','run 0 0']
    rows=run(native,commands,depth)
    assert rows[2][0]==(LIMIT if depth==1 else HELD)
    assert rows[2][1]==0 and rows[2][7]==-1 # Unwind releases only our hold.
    assert rows[-1][1]==3


@pytest.mark.parametrize('commands',[
    ['run 5 0'],['node 0 5 -1 -1 1 0','run 0 0'],
    ['node 0 -1 2 -1 1 0','run 0 0'],['owner 0 5','run 0 0']])
def test_invalid_graph_never_enters_callback(native,commands):
    assert run(native,commands,4)[-1][:4]==[INVALID,0,0,0]


@pytest.mark.parametrize('producer',[0,3])
def test_disabled_external_source_cannot_complete(native,producer):
    commands=['permission 0 0 0 0 0',f'node 0 -1 -1 -1 {producer} 0','external 0','run 0 0']
    rows=run(native,commands,4)
    assert rows[2][0]==0 and rows[-1][:4]==[SCOPE,0,0,0]


@pytest.mark.parametrize('depth',[1,2,4])
@pytest.mark.parametrize('producer',[0,3])
def test_external_completion_needs_no_nested_callback_stack(native,depth,producer):
    commands=['permission 0 0 0 1 1',f'node 0 1 -1 -1 1 0',f'node 1 -1 -1 -1 {producer} 0',
              'run 0 0','external 1','run 0 0']
    rows=run(native,commands,depth)
    assert rows[3][0]==PENDING and rows[3][2:4]==[1,1]
    assert rows[-1][:4]==[COMPLETED,3,1,2]


@pytest.mark.parametrize('depth',[1,2,4])
def test_nested_user_is_excluded_even_when_opted(native,depth):
    commands=['permission 0 2 1 1 1','node 0 1 -1 -1 1 0','node 1 -1 -1 -1 2 1','run 0 0','run 1 0','run 0 0']
    rows=run(native,commands,depth)
    assert rows[3][0]==USER
    assert rows[-1][1]==3


@pytest.mark.parametrize('profile',[0,1,2])
@pytest.mark.parametrize('kind',[1,2])
@pytest.mark.parametrize('opted',[0,1])
def test_scope_permission_does_not_expand_for_deeper_pump(native,profile,kind,opted):
    commands=[f'permission 1 {profile} {opted} 1 1',f'node 0 -1 -1 -1 {kind} {opted}','run 0 0']
    allowed=profile!=0 and (kind==1 or (profile==2 and opted))
    for depth in [1,2,4]:
        last=run(native,commands,depth)[-1]
        assert last[0]==(COMPLETED if allowed else SCOPE)


@pytest.mark.parametrize('context',[1,2,3])
def test_public_callback_entry_stays_forbidden(native,context):
    for depth in [1,2,4]:
        last=run(native,[f'run 0 {context}'],depth)[-1]
        assert last[:4]==[FORBIDDEN,0,0,0]


@pytest.mark.parametrize('owner',[-2,1,4])
def test_unavailable_resource_is_preserved(native,owner):
    last=run(native,[f'owner 0 {owner}','node 0 -1 0 -1 1 0','run 0 0'],4)[-1]
    assert last[:4]==[BUSY,0,0,0] and last[7]==owner


@pytest.mark.parametrize('seed',range(10))
def test_random_acyclic_graphs_finish_with_explicit_rounds(native,seed):
    rng=random.Random(seed)
    commands=[]
    # Reconfigure only nodes that are not yet done by using one fresh process
    # per graph; no hidden reset or fabricated independent trial count.
    dependencies=[rng.choice([-1,*range(i+1,5)]) for i in range(5)]
    commands=[f'node {i} {dep} -1 -1 1 0' for i,dep in enumerate(dependencies)]
    def reachable(i):return [i]+(reachable(dependencies[i]) if dependencies[i]>=0 else [])
    path=reachable(0)
    for depth,nested in [(1,False),(1,True),(2,True),(4,True)]:
        rows=run(native,commands+['run 0 0']+['round']*5,depth,nested)
        first=rows[5]
        expected=COMPLETED if len(path)<=depth else (LIMIT if nested else PENDING)
        assert first[0]==expected
        assert rows[-1][1]==31 and rows[-1][2]<=depth
