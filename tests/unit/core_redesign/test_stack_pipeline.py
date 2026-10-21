"""Same virtual-I/O protocol, continuation vs host stackful workflow. Not an MCU run."""
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
    app=tmp_path_factory.mktemp('two-stack')/'app'
    # ASan does not fully support makecontext/swapcontext. Host execution is
    # explicitly UBSan only; RISC-V register swaps are verified separately.
    command=[compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=undefined',
             '-fno-sanitize-recover=all',str(HERE/'stack_pipeline_driver.cpp'),'-o',str(app)]
    r=subprocess.run(command,capture_output=True,text=True,timeout=180)
    if dest:=os.environ.get('EXP03_TWO_STACK_EVIDENCE'):
        out=Path(dest);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),HERE/'stack_pipeline.hpp',HERE/'stack_pipeline_driver.cpp',
               REPO/'planning/core-redesign/two-stack-inputs.json',REPO/'planning/core-redesign/two-stack-traces.json']
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        report=dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,
                    compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),
                    inputs={str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
        if not r.returncode:
            shutil.copy2(app,out/'app');report['executable_sha256']=hashlib.sha256(app.read_bytes()).hexdigest()
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==0,r.stderr
    return app


def oracle(commands):
    # Independent unbounded Python values model of the four one-token channels.
    full=[False]*4;values=[0]*4
    displayed=completed=rejected=mismatch=request=result=0
    workflow='input';held=0;backend='request';rows=[]
    for command in commands:
        accepted=-1
        words=command.split()
        if words[0] in ['input','complete']:
            index=0 if words[0]=='input' else 2
            accepted=int(not full[index])
            if accepted:full[index]=True;values[index]=int(words[1])
            else:rejected=(rejected+1)&0xffffffff
        elif command=='turn':
            if workflow=='input' and full[0]:
                held=values[0];full[0]=False;workflow='send'
            if workflow=='send' and not full[1]:
                values[1]=held;full[1]=True;workflow='done'
            if workflow=='done' and full[3]:
                displayed=values[3];full[3]=False;completed=(completed+1)&0xffffffff;workflow='input'
            if backend=='request' and full[1]:
                request=values[1];full[1]=False;backend='raw'
            if backend=='raw' and full[2]:
                result=values[2];full[2]=False;mismatch=(mismatch+int(result!=request))&0xffffffff;backend='post'
            if backend=='post' and not full[3]:
                values[3]=result;full[3]=True;backend='request'
        else:assert command=='observe'
        rows.append([accepted,displayed,completed,rejected,mismatch,['request','raw','post'].index(backend),request,result,
                     *[v for pair in zip(map(int,full),values) for v in pair]])
    return rows


def run(native,commands,stackful):
    r=subprocess.run([str(native),str(int(stackful))],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert rows==oracle(commands)
    if dest:=os.environ.get('EXP03_TWO_STACK_EVIDENCE'):
        d=dict(stackful=stackful,commands=commands,rows=rows);key=hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()
        Path(dest,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(d,separators=(',',':')).encode(),mtime=0))
    return rows


TRACES=json.loads((REPO/'planning/core-redesign/two-stack-traces.json').read_text())


@pytest.mark.parametrize('name',TRACES)
def test_fixed_virtual_io(native,name):
    commands=TRACES[name]
    assert run(native,commands,False)==run(native,commands,True)


def test_delayed_completion_keeps_request_across_yields(native):
    commands=['input 4294967295','turn']+['turn']*1000+['complete 4294967295','turn','turn']
    for mode in [False,True]:
        rows=run(native,commands,mode)
        assert rows[-1][1:5]==[4294967295,1,0,0]
        assert rows[-3][2]==0


@pytest.mark.parametrize('seed',range(10))
def test_random_input_completion_and_stalls(native,seed):
    rng=random.Random(seed);commands=[]
    for _ in range(10000):
        kind=rng.randrange(5)
        commands.append(f'input {rng.randrange(2**32)}' if kind==0 else f'complete {rng.randrange(2**32)}' if kind==1 else 'turn' if kind<4 else 'observe')
    assert run(native,commands,False)==run(native,commands,True)
