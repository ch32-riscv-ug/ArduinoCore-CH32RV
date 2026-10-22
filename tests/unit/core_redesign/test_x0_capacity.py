"""N07 capacity4 -> capacity1, independent PPS rejection projection."""
import difflib
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
spec=importlib.util.spec_from_file_location('capacity_reference',HERE/'app_oracle.py')
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('x0-capacity')
    original=(HERE/'x0_features_driver.cpp').read_text()
    prefix=original[:original.index('template<unsigned Candidate> void select')]
    def translation(header):
        tail='''
template<unsigned C> void choose(bool touch,WorkDomain domain,bool opt){
 if(touch)protocol<ExerciseApplication<C,true>,false>(domain,opt,true);
 else protocol<ExerciseApplication<C,false>,false>(domain,opt,false);
}
int main(int argc,char** argv){
 assert(argc==5);unsigned c=std::strtoul(argv[1],nullptr,10);
 bool touch=std::strtoul(argv[2],nullptr,10)!=0,opt=std::strtoul(argv[4],nullptr,10)!=0;
 auto domain=WorkDomain(std::strtoul(argv[3],nullptr,10));
 if(c==1)choose<1>(touch,domain,opt);
 else if(c==2)choose<2>(touch,domain,opt);
 else if(c==3)choose<3>(touch,domain,opt);
 else {assert(c==4);choose<4>(touch,domain,opt);}
}
'''
        return prefix.replace('#include "x0_features.hpp"','#include "'+str(header)+'"')+tail
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    paths=[Path(__file__),REPO/'planning/core-redesign/change-exercises-inputs.json',HERE/'x0_features_driver.cpp',HERE/'app_oracle.py',HERE/'oracle.py']
    paths += [HERE/n for n in ['x0_capacity.hpp','x0_features.hpp','x0_candidates.hpp','r_application.hpp','r_workloads.hpp','static_mailbox.hpp','event_executor.hpp','work_executor.hpp','data_parts.hpp']]
    paths += [REPO/'planning/core-redesign/tasks/EXP-07/queue-change'/n for n in ['before.cpp','after.cpp']]
    out=None;report=dict(compiler_version=subprocess.check_output([compiler,'--version'],text=True),compiler_sha256=sha(compiler),inputs={str(p.relative_to(REPO)):sha(p) for p in paths},builds=[])
    if destination:=os.environ.get('EXP07_QUEUE_CHANGE_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
    binaries={}
    for name in ['before','after']:
        source=directory/(name+'.cpp');header=REPO/'planning/core-redesign/tasks/EXP-07/queue-change'/(name+'.cpp')
        source.write_text(translation(header));binary=directory/name
        command=[compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(HERE),str(source),'-o',str(binary)]
        p=subprocess.run(command,capture_output=True,text=True,timeout=240)
        b=dict(name=name,command=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,generated_sha256=sha(source))
        if not p.returncode:b['executable_sha256']=sha(binary)
        report['builds'].append(b)
        if out:
            shutil.copy2(source,out/(name+'.cpp'))
            if not p.returncode:shutil.copy2(binary,out/name)
            (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
        assert p.returncode==0,p.stderr
        binaries[name]=binary
    if out:
        before=(paths[-2]).read_text().splitlines(keepends=True);after=paths[-1].read_text().splitlines(keepends=True)
        (out/'selection.patch').write_text(''.join(difflib.unified_diff(before,after,fromfile='before.cpp',tofile='after.cpp')))
    return binaries


def expected(commands,capacity,touch,domain,opt):
    model=reference.ApplicationOracle(touch_period=20);model.apply(f'touch {int(touch)}');rows=[]
    for command in commands:
        if command.startswith('key ') and len(model.pd.commands)>=capacity:
            model.pd.rejected+=1;row=model.observe()
        elif command.startswith('opportunity '):
            where,profile,context=map(int,command.split()[1:])
            allowed=context==0 and (where==0 or (profile!=0 and (domain==0 or (profile==2 and opt))))
            row=model.apply('step') if allowed else model.observe()
        else:row=model.apply(command)
        rows.append(row)
    return rows


def run(native,commands,candidate=1,changed=False,touch=False,domain=0,opt=False):
    name='after' if changed else 'before';p=subprocess.run([str(native[name]),str(candidate),str(int(touch)),str(domain),str(int(opt))],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert p.returncode==0,p.stderr;assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()]
    assert rows==expected(commands,1 if changed else 4,touch,domain,opt)
    for r in rows:assert r['started']-r['reclaimed']==r['active'] and r['completed']-r['reclaimed']==r['terminal'] and not r['corruptions']
    if destination:=os.environ.get('EXP07_QUEUE_CHANGE_EVIDENCE'):
        d=dict(candidate=candidate,changed=changed,touch=touch,domain=domain,opt=opt,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest();Path(destination,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(d,separators=(',',':')).encode(),mtime=0))
    return rows


def ticks(first,last):return [c for t in range(first,last) for c in [f'time {t}','step']]


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('touch',[False,True])
def test_capacity_reduction_can_reject_apply_after_increment(native,candidate,touch):
    commands=['attach','key 1','key 0','step']+ticks(0,500)
    before=run(native,commands,candidate,False,touch);after=run(native,commands,candidate,True,touch)
    assert before[-1]['confirmed']==5020 and before[-1]['accepted']==2
    assert after[-1]['desired']==5020 and after[-1]['confirmed']==0 and after[-1]['rejected']==1
    assert not after[-1]['valid'] and after[-1]['pd_started']==0


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('touch',[False,True])
def test_explicit_producer_spacing_preserves_admitted_controls(native,candidate,touch):
    commands=['attach','key 1','step','key 0','step']+ticks(0,500)
    before=run(native,commands,candidate,False,touch);after=run(native,commands,candidate,True,touch)
    assert before==after and after[-1]['confirmed']==5020 and not after[-1]['rejected']


@pytest.mark.parametrize('domain,opt',[(0,False),(1,False),(1,True)])
@pytest.mark.parametrize('profile',[0,1,2])
def test_suppressed_opportunity_does_not_make_queue_space(native,domain,opt,profile):
    commands=['attach','key 1',f'opportunity 2 {profile} 0','key 0','step']+ticks(0,10)
    for c in range(1,5):
        after=run(native,commands,c,True,False,domain,opt)
        allowed=profile!=0 and (domain==0 or (profile==2 and opt))
        assert after[-1]['rejected']==int(not allowed)


@pytest.mark.parametrize('changed',[False,True])
@pytest.mark.parametrize('touch',[False,True])
def test_disconnect_and_delayed_completion_keep_session_semantics(native,changed,touch):
    commands=['attach','key 1','key 0','step','detach','attach','key 0','step','finish 1 1 1']+ticks(0,500)
    rows=[run(native,commands,c,changed,touch) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert rows[0][-1]['confirmed']==5020 and rows[0][-1]['valid']


@pytest.mark.parametrize('seed',range(10))
def test_random_same_stimuli_independent_capacity_oracle(native,seed):
    randomizer=random.Random(seed);commands=['attach'];time=0
    for _ in range(1000):
        choice=randomizer.randrange(10)
        if choice<4:commands += [f'key {randomizer.choice([-1,0,1])}']
        elif choice<7:time+=randomizer.randrange(1,5);commands += [f'time {time}','step']
        elif choice==7:commands+=['detach','attach']
        elif choice==8:commands+=['fault 1']
        else:commands+=['step']
    commands+=ticks(time,time+300)
    for changed in [False,True]:
        rows=[run(native,commands,c,changed,bool(seed&1)) for c in range(1,5)]
        assert all(r==rows[0] for r in rows)
