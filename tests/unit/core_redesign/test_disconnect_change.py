"""N07 full X0 policy addition: fail closed versus reconnect after real lease collection."""
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
spec=importlib.util.spec_from_file_location('connection_reference',HERE/'connection_oracle.py')
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


def generated(header):
    source=(HERE/'x0_features_driver.cpp').read_text();source=source[:source.index('template<unsigned Candidate> void select')]
    source=source.replace('#include "x0_features.hpp"','#include "'+str(header)+'"')
    source=source.replace('long a = 0, b = 0, c = 0;','long a = 0, b = 0, c = 0, d = 0, e = 0;').replace('a = b = c = 0;','a = b = c = d = e = 0;')
    source=source.replace('"%31s %ld %ld %ld", op, &a, &b, &c','"%31s %ld %ld %ld %ld %ld", op, &a, &b, &c, &d, &e')
    source=source.replace('if (!std::strcmp(op, "attach")) app.pps.attach();','int status=99;\n        if (!std::strcmp(op, "attach")) status=int(app.pps.attach());')
    source=source.replace('app.pps.enqueue(int16_t(a));','status=app.pps.enqueue(int16_t(a));').replace('app.pps.completion(uint32_t(a), uint32_t(b), c != 0);','status=app.pps.completion(uint32_t(a), uint32_t(b), c != 0);')
    source=source.replace('else if (!std::strcmp(op, "step")) app.opportunity','else if (!std::strcmp(op,"hold")) app.pps.auto_hardware=a==0;\n        else if (!std::strcmp(op,"terminal")) status=app.pps.finish(uint32_t(a),uint32_t(b),IoResult(c),unsigned(d),e!=0);\n        else if (!std::strcmp(op,"mutate")) status=app.pps.buffer.write(unsigned(a),uint8_t(b));\n        else if (!std::strcmp(op,"inspect")) {}\n        else if (!std::strcmp(op, "step")) status=int(app.opportunity')
    source=source.replace('WorkOpportunity::Explicit, WorkProfile::M);','WorkOpportunity::Explicit, WorkProfile::M));')
    source=source.replace('app.opportunity(WorkOpportunity(a), WorkProfile(b), WorkContext(c));','status=int(app.opportunity(WorkOpportunity(a), WorkProfile(b), WorkContext(c)));')
    source=source.replace('            std::printf("{', '            std::printf("{\\\"application\\\":");\n            std::printf("{',1)
    source=source.replace('%u}\\n",','%u}",')
    anchor='app.missed_measure, touch_missed<Runtime>(app));'
    assert anchor in source
    source=source.replace(anchor,anchor+'''
        auto& p=app.pps;
        std::printf(",\\\"status\\\":%d,\\\"connection\\\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\\\"payload\\\":[",
            status,p.epoch(),unsigned(p.connected),unsigned(p.io.opened()),unsigned(p.io.active()),unsigned(p.io.terminal()),unsigned(p.io.cancel_pending()),p.io.token(),unsigned(p.buffer.available()),p.discarded,unsigned(p.last.result),p.last.transferred,p.last.digest);
        if(p.buffer.available())for(unsigned i=0;i<8;++i){uint8_t value=0;p.buffer.read(i,value);std::printf("%s%u",i?",":"",unsigned(value));}
        std::printf("]}\\n");
''')
    source+='''
template<unsigned C> void choose(bool touch,WorkDomain d,bool opt){
 if(touch)protocol<ExerciseApplication<C,true>,false>(d,opt,true);else protocol<ExerciseApplication<C,false>,false>(d,opt,false);
}
int main(int argc,char** argv){
 assert(argc==5);unsigned c=std::strtoul(argv[1],nullptr,10);bool touch=std::strtoul(argv[2],nullptr,10)!=0;
 auto d=WorkDomain(std::strtoul(argv[3],nullptr,10));bool opt=std::strtoul(argv[4],nullptr,10)!=0;
 if(c==1)choose<1>(touch,d,opt);else if(c==2)choose<2>(touch,d,opt);else if(c==3)choose<3>(touch,d,opt);else{assert(c==4);choose<4>(touch,d,opt);}
}
'''
    return source


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    directory=tmp_path_factory.mktemp('disconnect-change');compiler=shutil.which('g++');assert compiler
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    paths=[Path(__file__),HERE/'connection_oracle.py',HERE/'app_oracle.py',HERE/'oracle.py',HERE/'x0_features_driver.cpp']
    paths += [HERE/n for n in ['connection_types.hpp','connection_pps.hpp','connection_x0.hpp','io_lifecycle.hpp','x0_features.hpp','x0_candidates.hpp','r_application.hpp','r_workloads.hpp','static_mailbox.hpp','event_executor.hpp','work_executor.hpp','data_parts.hpp']]
    folder=REPO/'planning/core-redesign/tasks/EXP-07/disconnect-change';paths+=sorted(folder.glob('*'))
    report=dict(inputs={str(p.relative_to(REPO)):sha(p) for p in paths},compiler_sha256=sha(compiler),compiler_version=subprocess.check_output([compiler,'--version'],text=True),builds=[]);out=None
    if destination:=os.environ.get('EXP07_DISCONNECT_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
    binaries={}
    for name in ['before','after']:
        source=directory/(name+'.cpp');source.write_text(generated(folder/(name+'.cpp')));binary=directory/name
        command=[compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(HERE),str(source),'-o',str(binary)]
        p=subprocess.run(command,capture_output=True,text=True,timeout=240);b=dict(name=name,command=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,generated_sha256=sha(source))
        if not p.returncode:b['executable_sha256']=sha(binary)
        report['builds'].append(b)
        if out:
            shutil.copy2(source,out/(name+'.cpp'))
            if not p.returncode:shutil.copy2(binary,out/name)
            (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
        assert p.returncode==0,p.stderr;binaries[name]=binary
    return binaries


def run(native,commands,candidate=1,changed=True,touch=False,domain=0,opt=False):
    p=subprocess.run([str(native['after' if changed else 'before']),str(candidate),str(int(touch)),str(domain),str(int(opt))],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert p.returncode==0,p.stderr;assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];model=reference.Model(changed,touch,domain,opt)
    assert len(rows)==len(commands)
    for command,row in zip(commands,rows):
        expected=model.apply(command);assert row==expected,(command,row,expected)
        assert row['connection'][3]==1-row['connection'][7]
    if destination:=os.environ.get('EXP07_DISCONNECT_EVIDENCE'):
        d=dict(candidate=candidate,changed=changed,touch=touch,domain=domain,opt=opt,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest();Path(destination,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(d,separators=(',',':')).encode(),mtime=0))
    return rows


def ticks(first,last):return [c for t in range(first,last) for c in [f'time {t}','step']]


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('touch',[False,True])
def test_no_disconnect_same_complete_application_behavior(native,candidate,touch):
    commands=['attach','key 1','key 0','step']+ticks(0,500)
    before=run(native,commands,candidate,False,touch);after=run(native,commands,candidate,True,touch)
    assert before==after and after[-1]['application']['confirmed']==5020


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('touch',[False,True])
def test_added_reconnect_waits_for_old_backend_stop_and_collection(native,candidate,touch):
    commands=['attach','hold 1','key 1','key 0','step','detach','attach','key 0','step','mutate 0 99','terminal 1 1 3 8 0','step','terminal 1 1 3 8 1','inspect','step','terminal 1 1 1 8 1','terminal 2 2 1 8 1','step']+ticks(0,500)
    rows=run(native,commands,candidate,True,touch)
    assert rows[8]['connection'][2:8]==[0,1,0,1,1,0]
    assert rows[9]['status']==rows[10]['status']==0 and rows[11]['connection'][3:6]==[1,0,1]
    assert rows[13]['connection'][3:8]==[1,1,1,1,0]
    assert rows[14]['connection'][2:8]==[1,1,0,0,2,0]
    assert rows[15]['status']==0 and rows[16]['status']==1
    assert rows[-1]['application']['confirmed']==5020 and rows[-1]['application']['valid']
    baseline=run(native,commands,candidate,False,touch)
    assert baseline[6]['status']==2 and not baseline[-1]['application']['valid'] and baseline[-1]['connection'][7]


@pytest.mark.parametrize('outcome',[1,2,3])
@pytest.mark.parametrize('changed',[False,True])
def test_old_result_never_revalidates_link_after_disconnect(native,outcome,changed):
    commands=['attach','hold 1','key 0','step','detach','attach',f'terminal 1 1 {outcome} 8 1','step','inspect']
    rows=[run(native,commands,c,changed) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert not rows[0][-1]['application']['valid'] and rows[0][-1]['application']['pd_reclaimed']==1


@pytest.mark.parametrize('touch',[False,True])
def test_old_queued_input_dropped_and_new_request_requires_new_apply(native,touch):
    commands=['attach','key 1','key 0','detach','attach','step']+ticks(0,100)+['key 0','step']+ticks(100,500)
    rows=[run(native,commands,c,True,touch) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert rows[0][5]['connection'][8]==2 and rows[0][5]['application']['pd_started']==0 and rows[0][5]['application']['desired']==5000
    assert rows[0][-1]['application']['confirmed']==5000


@pytest.mark.parametrize('domain,opt',[(0,False),(1,False),(1,True)])
@pytest.mark.parametrize('profile',[0,1,2])
def test_recovery_does_not_override_progress_profile(native,domain,opt,profile):
    commands=['attach','hold 1','key 0','step','detach','attach','terminal 1 1 3 8 1',f'opportunity 2 {profile} 0','inspect','step']
    allowed=profile!=0 and (domain==0 or (profile==2 and opt))
    for c in range(1,5):
        rows=run(native,commands,c,True,False,domain,opt)
        assert rows[8]['connection'][3]==int(not allowed)
        assert rows[-1]['connection'][2:4]==[1,0]


@pytest.mark.parametrize('seed',range(10))
def test_generated_disconnect_and_retry_controls(native,seed):
    rng=random.Random(seed);commands=['attach'];time=0
    for _ in range(1000):
        x=rng.randrange(10)
        if x<4:commands+=[f'key {rng.choice([-1,0,1])}']
        elif x<7:time+=rng.randrange(1,6);commands+=[f'time {time}','step']
        elif x==7:commands+=['detach','attach']
        elif x==8:commands+=['fault 1']
        else:commands+=['step']
    commands+=ticks(time,time+300)
    for changed in [False,True]:
        rows=[run(native,commands,c,changed,bool(seed&1)) for c in range(1,5)];assert all(r==rows[0] for r in rows)


@pytest.mark.parametrize('outcome',[1,2])
@pytest.mark.parametrize('touch',[False,True])
def test_terminal_already_held_when_detach_does_not_restore_confirmation(native,outcome,touch):
    commands=['attach','hold 1','key 0','step',f'terminal 1 1 {outcome} 8 1','detach','attach','step','inspect']
    rows=[run(native,commands,c,True,touch) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert rows[0][-1]['application']['valid']==0 and rows[0][-1]['connection'][2:8]==[1,0,0,0,1,1]
    assert rows[0][-1]['connection'][9]==outcome


@pytest.mark.parametrize('touch',[False,True])
@pytest.mark.parametrize('changed',[False,True])
def test_no_stop_witness_keeps_old_lease_while_other_application_work_progresses(native,touch,changed):
    commands=['attach','hold 1','key 0','step','detach','attach']+ticks(0,500)+['inspect']
    rows=[run(native,commands,c,changed,touch) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    last=rows[0][-1];assert last['connection'][2:8]==[0,1,0,1,1,0]
    assert last['application']['measure_done']==5 and last['application']['touch_done']==(25 if touch else 0)
    assert last['application']['pd_reclaimed']==0


def test_full_old_queue_can_refuse_new_session_input_until_explicit_drain(native):
    commands=['attach']+['key 1']*4+['detach','attach','key 0','step','key 0','step']+ticks(0,10)
    rows=[run(native,commands,c) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert rows[0][7]['status']==0 and rows[0][8]['connection'][8]==4
    assert rows[0][-1]['application']['confirmed']==5000 and rows[0][-1]['application']['rejected']==1


@pytest.mark.parametrize('outcome',[2,3])
@pytest.mark.parametrize('count',[0,2,8])
def test_partial_old_terminal_returns_buffer_only_after_normal_collection(native,outcome,count):
    commands=['attach','hold 1','key 0','step','detach','attach',f'terminal 1 1 {outcome} {count} 1','mutate 0 99','step','inspect']
    rows=[run(native,commands,c) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert rows[0][7]['status']==0 and not rows[0][-1]['application']['valid']
    assert rows[0][-1]['connection'][7] and rows[0][-1]['connection'][9:11]==[outcome,count]


@pytest.mark.parametrize('context',[1,2,3])
def test_forbidden_context_keeps_terminal_for_normal_recovery(native,context):
    commands=['attach','hold 1','key 0','step','detach','attach','terminal 1 1 3 8 1',f'opportunity 0 0 {context}','inspect','step']
    rows=[run(native,commands,c) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    assert rows[0][8]['connection'][3:6]==[1,1,1]
    assert rows[0][-1]['connection'][2:4]==[1,0]
