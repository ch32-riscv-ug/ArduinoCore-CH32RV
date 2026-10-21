"""Fixed FreeRTOS kernel under a deterministic native test port, not WCH execution."""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

HERE=Path(__file__).parent
REPO=HERE.parents[2]
FIXTURE=REPO/'tests/platform/core_redesign/kernel_trace'
LOCK=REPO/'planning/core-redesign/kernel-trace-inputs.json'
TRACES=json.loads((REPO/'planning/core-redesign/kernel-trace-traces.json').read_text())
spec=importlib.util.spec_from_file_location('kernel_trace_reference',HERE/'test_stack_pipeline.py')
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    source=Path(os.environ.get('EXP03_KERNEL_SOURCE',REPO/'work/exp03-stack-port-sources-20261022'))
    if not source.is_dir():
        if 'EXP03_KERNEL_SOURCE' in os.environ or 'EXP03_KERNEL_TRACE_EVIDENCE' in os.environ:
            pytest.fail('Explicit kernel experiment requires the locked vendor source directory')
        pytest.skip('Requires explicitly locked vendor sources; no automatic network or board access')
    lock=json.loads(LOCK.read_text())
    for row in lock['sources']:
        p=source/row['path'];assert p.stat().st_size==row['bytes'] and sha(p)==row['sha256']
        data=p.read_bytes();assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==row['git_blob']
    gcc=shutil.which('gcc');gxx=shutil.which('g++');assert gcc and gxx
    kernel=source/'EVT/EXAM/FreeRTOS/FreeRTOS_Core/FreeRTOS'
    directory=tmp_path_factory.mktemp('kernel-trace');out=None
    report=dict(status='running',scope='Host LP64 fixed kernel execution; native port replaces WCH hardware port',commands=[],inputs={},vendor_sources_verified=len(lock['sources']),executables={})
    if dest:=os.environ.get('EXP03_KERNEL_TRACE_EVIDENCE'):
        out=Path(dest);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),LOCK,REPO/'planning/core-redesign/kernel-trace-traces.json',HERE/'stack_pipeline_driver.cpp',HERE/'stack_pipeline.hpp',HERE/'test_stack_pipeline.py',
               *[p for p in FIXTURE.rglob('*') if p.suffix in ['.c','.cpp','.h']]]
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target);report['inputs'][str(p.relative_to(REPO))]=sha(p)
        for row in lock['sources']:
            target=out/'source'/row['path'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/row['path'],target)
    def save():
        if out:(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    def command(args):
        r=subprocess.run([str(a) for a in args],capture_output=True,text=True,timeout=180)
        report['commands'].append(dict(command=[str(a) for a in args],returncode=r.returncode,stdout=r.stdout,stderr=r.stderr));save()
        assert r.returncode==0,r.stderr
    report['compilers']={c:dict(sha256=sha(c),version=subprocess.check_output([c,'--version'],text=True)) for c in [gcc,gxx]}
    binaries={}
    for preempt in [0,1]:
        for tasks in [2,3]:
            name=f'kernel-p{preempt}-tasks{tasks}';objects=[]
            flags=['-O1','-g1','-fsanitize=undefined','-fno-sanitize-recover=all','-D_XOPEN_SOURCE=700','-DEXP_HOST=1',f'-DEXP_TASKS={tasks}',f'-DEXP_PREEMPT={preempt}','-DEXP_STACK_WORDS=8192',
                   f'-I{FIXTURE/"host"}',f'-I{FIXTURE}',f'-I{kernel/"include"}']
            for p in [FIXTURE/'fixture.c',FIXTURE/'host/port.c',kernel/'tasks.c',kernel/'queue.c',kernel/'list.c',FIXTURE/'host/driver.cpp']:
                obj=directory/f'{name}-{p.name}.o';objects.append(obj)
                command([gxx if p.suffix=='.cpp' else gcc,*flags,'-c',p,'-o',obj])
            binary=directory/name;command([gxx,'-fsanitize=undefined',*objects,'-o',binary]);binaries[name]=binary
    binary=directory/'reference'
    command([gxx,'-std=c++17','-O1','-g1','-fsanitize=undefined','-fno-sanitize-recover=all',HERE/'stack_pipeline_driver.cpp','-o',binary]);binaries['reference']=binary
    for name,binary in binaries.items():
        report['executables'][name]=sha(binary)
        if out:shutil.copy2(binary,out/name)
    report['status']='passed';save()
    return binaries


def normalize(row):
    row=row.copy()
    for i in range(8,16,2):
        if not row[i]:row[i+1]=0
    return row


def expanded(commands):
    result=[];checkpoints=[]
    for command in commands:
        result+=['turn']*32 if command=='drain' else [command]
        checkpoints.append(len(result)-1)
    return result,checkpoints


def run(native,commands,name):
    if name.startswith('reference'):
        actual,checkpoints=expanded(commands);binary=native['reference'];args=[str(int(name.endswith('stack')))]
    else:actual=commands;checkpoints=list(range(len(commands)));binary=native[name];args=[]
    r=subprocess.run([str(binary),*args],input='\n'.join(actual)+'\n',capture_output=True,text=True,timeout=90)
    assert r.returncode==0,r.stderr;assert not r.stderr
    allrows=[json.loads(line) for line in r.stdout.splitlines()];assert len(allrows)==len(actual)
    rows=[normalize(allrows[i]) for i in checkpoints]
    if dest:=os.environ.get('EXP03_KERNEL_TRACE_EVIDENCE'):
        d=dict(name=name,commands=commands,rows=rows,expanded_commands=len(actual))
        key=hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()
        Path(dest,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(d,separators=(',',':')).encode(),mtime=0))
    return rows


def expected(commands):
    actual,points=expanded(commands);rows=reference.oracle(actual)
    return [normalize(rows[i]) for i in points]


@pytest.mark.parametrize('name',TRACES)
def test_two_task_kernel_and_both_references(native,name):
    commands=TRACES[name];wanted=expected(commands)
    for binary in ['reference-continuation','reference-stack','kernel-p0-tasks2','kernel-p1-tasks2']:
        assert run(native,commands,binary)==wanted,(name,binary)


def test_driver_freezes_workers_inside_batch(native):
    commands=['input 1','complete 1','observe','observe','drain']
    for name in ['kernel-p0-tasks2','kernel-p1-tasks2','kernel-p0-tasks3','kernel-p1-tasks3']:
        rows=run(native,commands,name)
        assert all(r[2]==0 for r in rows[:-1])
        assert rows[-1][1:5]==[1,1,0,0]


def test_three_stage_pipeline_has_more_inflight_capacity(native):
    commands=TRACES['stage-capacity']
    for preempt in [0,1]:
        two=run(native,commands,f'kernel-p{preempt}-tasks2')
        three=run(native,commands,f'kernel-p{preempt}-tasks3')
        accepted_two=[r[0] for c,r in zip(commands,two) if c.startswith('input')]
        accepted_three=[r[0] for c,r in zip(commands,three) if c.startswith('input')]
        assert accepted_two==[1,1,0,0,0]
        assert accepted_three==[1,1,1,1,0]
        assert two[-1][2]==2 and three[-1][2]==4


@pytest.mark.parametrize('seed',range(10))
def test_random_canonical_batches(native,seed):
    rng=random.Random(seed);commands=[]
    for _ in range(3000):
        k=rng.randrange(5)
        commands.append(f'input {rng.randrange(2**32)}' if k==0 else f'complete {rng.randrange(2**32)}' if k==1 else 'drain' if k<4 else 'observe')
    wanted=expected(commands)
    for name in ['reference-continuation','reference-stack','kernel-p0-tasks2','kernel-p1-tasks2']:
        assert run(native,commands,name)==wanted,(seed,name)
