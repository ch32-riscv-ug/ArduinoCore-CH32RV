"""Managed register I2C, scoped callbacks and retained completion hints."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;ROOT=UNIT.parents[2]
spec=importlib.util.spec_from_file_location('i2c_event_protocol',UNIT/'test_i2c_managed_handoff.py')
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('i2c-event')
    original=(UNIT/'i2c_master_driver.cpp').read_text();a=original.index('struct Fake {');b=original.index('template<bool Guarded',a)
    generated=directory/'i2c_handoff_fake.inc';generated.write_text(original[a:b])
    flags=['-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    evidence=os.environ.get('EXP03_I2C_EVENT_EVIDENCE');out=Path(evidence) if evidence else None
    report=dict(compiler=compiler,compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),fake_sha256=hashlib.sha256(generated.read_bytes()).hexdigest(),inputs={},builds=[])
    if out:out.mkdir(parents=True,exist_ok=False);shutil.copy2(generated,out/generated.name)
    names=['i2c_event_driver.cpp','test_i2c_event.py','completion_delivery.hpp','event_executor.hpp',
           'work_executor.hpp','data_parts.hpp','i2c_managed_handoff.hpp','i2c_managed_handoff_driver.cpp',
           'i2c_handoff.hpp','i2c_handoff_driver.cpp','i2c_resources.hpp','i2c_master_driver.cpp',
           'test_i2c_master.py','test_i2c_handoff.py','test_i2c_managed_handoff.py']
    for path in [*(UNIT/name for name in names),ROOT/'tests/platform/core_redesign/runtime/i2c_master.hpp']:
        relative=path.relative_to(ROOT);report['inputs'][str(relative)]=hashlib.sha256(path.read_bytes()).hexdigest()
        if out:
            dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    binaries={}
    for label,source in [('delivery','i2c_event_driver.cpp'),('baseline','i2c_managed_handoff_driver.cpp')]:
        executable=directory/label;command=[compiler,*flags,'-I',str(directory),str(UNIT/source),'-o',str(executable)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=180)
        build=dict(name=label,command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr);report['builds'].append(build)
        if out:(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
        assert result.returncode==0,result.stderr
        build['executable_sha256']=hashlib.sha256(executable.read_bytes()).hexdigest()
        if out:shutil.copy2(executable,out/label);(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
        binaries[label]=executable
    return binaries,out


def run(native,commands,policy='dedicated',mode='normal',baseline=False):
    binaries,evidence=native;name='baseline' if baseline else 'delivery';args=[policy,mode]
    stdin='\n'.join(commands)+'\n';result=subprocess.run([str(binaries[name]),*args],input=stdin,capture_output=True,text=True,timeout=60)
    if evidence:
        key=hashlib.sha256(json.dumps([name,args,stdin]).encode()).hexdigest()
        (evidence/(key+'.json')).write_text(json.dumps(dict(program=name,args=args,stdin=stdin,stdout=result.stdout,stderr=result.stderr,returncode=result.returncode),indent=2)+'\n')
    assert result.returncode==0,result.stderr;assert not result.stderr
    rows=[json.loads(line) for line in result.stdout.splitlines()];assert len(rows)==len(commands)
    if not baseline:
        for command,row in zip(commands,rows):
            if command.split()[0] not in ['backend','close','dispatch','step']:assert row['events']==[],(command,row)
            if row['backend'][0]:assert row['held'][row['selected']]
            assert sum(row['held'])<=1
            if any(row['held']):assert row['resource'][0]>0 and row['resource'][2]>=1 and row['resource'][3]==7 and not row['resource'][4]
            if row['backend'][1]:assert row['current']<len(row['records']) and row['records'][row['current']][0]==2
    return rows


def ready():return ['change','publish 48000000','pins 1','backend 400000','open 0','open 1','open 2']
def wire(tx,rx,values=None):return protocol.wire(tx,rx,values)
def collected(rows):protocol.collected(rows)


def complete(client=0,buffer=0,value=88):
    return [f'submit {client} {buffer} 0 1 {60+client}','dispatch',*wire(0,1,[value]),'reap']


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('tx,rx',[(0,n) for n in [1,2,3,4,8,32,64]]+[(n,0) for n in [1,2,8,32,64]]+[(2,n) for n in [1,2,3,8,32]])
def test_callback_collection_matches_direct_register_path(native,policy,tx,rx):
    commands=[*ready(),'initialize','bind 0 0'];baseline=ready();pairs=[(i,i) for i in range(7)]
    def pair(new,old=None):
        commands.append(new);baseline.append(new if old is None else old);pairs.append((len(commands)-1,len(baseline)-1))
    for i in range(tx):pair(f'write 0 {i} {11+i}')
    pair(f'submit 0 0 {tx} {rx} 60');pair('dispatch')
    for command in wire(tx,rx):pair(command)
    pair('pump 1 0 0','snapshot');premature=len(commands)-1
    pair('reap');pair('pump 1 0 0','snapshot');pumped=len(commands)-1
    pair('turn 1 0 2 0','take 1 0 0');delivered=len(commands)-1
    pair('close')
    actual=run(native,commands,policy);control=run(native,baseline,policy,baseline=True)
    for i,j in pairs:
        for key in control[j]:
            if key not in ['status','out']:assert actual[i][key]==control[j][key],(commands[i],key)
    assert actual[premature]['status']==3 and actual[premature]['callbacks']==[[0,0,0],[0,0,0]]
    assert actual[pumped]['status']==0 and actual[pumped]['bytes'][0]==[-1]*64
    assert actual[delivered]['callback_out'][0]==control[-2]['out']==[1,tx,tx,rx,0]
    assert actual[-1]['callbacks']==[[1,1,0],[0,0,0]];collected(actual)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('domain',range(3))
@pytest.mark.parametrize('profile',range(3))
@pytest.mark.parametrize('opportunity',range(3))
def test_delivery_permissions_after_backend_resources_returned(native,policy,domain,profile,opportunity):
    commands=[*ready(),'initialize',f'bind {int(domain!=0)} {int(domain==2)}',*complete(),'close','change','publish 12000000','pump 1 0 0',f'turn 1 {opportunity} {profile} 0','notice 1 0 0','turn 1 0 2 0']
    rows=run(native,commands,policy)
    eligible=opportunity==0 or (profile!=0 and (domain==0 or (profile==2 and domain==2)))
    assert rows[-3]['status']==(4 if opportunity and profile==0 else 0)
    assert rows[-3]['callbacks'][0]==[int(eligible),int(eligible),0]
    assert rows[-2]['status']==int(not eligible)
    assert rows[-3]['resource']==[12000000,2,0,0,0] and rows[-3]['held']==[0,0]
    assert rows[-1]['callbacks'][0]==[1,1,0] and rows[-1]['callback_out'][0]==[1,0,0,1,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_full_queue_does_not_block_next_transaction_or_erase_pending_result(native,policy):
    commands=[*ready(),'initialize','bind 0 0',*complete(),'pump 1 0 0',*complete(1,1,99),'close','pump 1 1 1','notice 1 1 1','turn 1 0 2 0','pump 1 1 1','turn 1 0 2 0']
    rows=run(native,commands,policy)
    assert rows[-5]['status']==1 and rows[-4]['status']==1
    assert rows[-5]['bytes'][0]==rows[-5]['bytes'][1]==[-1]*64
    assert rows[-5]['held']==[0,0]
    assert rows[-3]['callbacks'][0]==[1,1,0] and rows[-3]['bytes'][1]==[-1]*64
    assert rows[-1]['callbacks'][0]==[2,2,0] and rows[-1]['bytes'][1][0]==99;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_old_hint_cannot_collect_reused_record(native,policy):
    commands=[*ready(),'initialize','bind 0 0',*complete(),'pump 1 0 0','take 1 0 0',*complete(value=99),'close','pump 2 0 0','turn 1 0 2 0','notice 2 0 0','pump 2 0 0','turn 1 0 2 0']
    rows=run(native,commands,policy)
    assert rows[-5]['status']==1 and rows[-4]['callbacks'][0]==[1,0,1]
    assert rows[-4]['callback_out'][0]==[0,0,0,0,0] and rows[-4]['bytes'][0]==[-1]*64
    assert rows[-3]['status']==1 and rows[-1]['callbacks'][0]==[2,1,1]
    assert rows[-1]['bytes'][0][0]==99;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('ack',[False,True])
def test_client_end_or_ack_after_enqueue_requires_explicit_result_cleanup(native,policy,ack):
    commands=[*ready(),'initialize','bind 0 0',*complete(),'pump 1 0 0','ack 1 0 0' if ack else 'end 0','close','turn 1 0 2 0','notice 1 0 0','take 1 0 0']
    rows=run(native,commands,policy)
    assert rows[-3]['callbacks'][0]==[1,0,1] and rows[-3]['bytes'][0]==[-1]*64
    assert rows[-3]['held']==[0,0] and rows[-2]['status']==0
    assert rows[-1]['out']==[1,0,0,1,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('early_ack',[False,True])
def test_target_removal_keeps_source_for_retry_unless_caller_acknowledged(native,policy,early_ack):
    commands=[*ready(),'initialize','bind 0 0',*complete(),'close','pump 1 0 0']
    if early_ack:commands+=['ack 1 0 0']
    commands+=['unbind','bind 0 0','turn 1 0 2 0','pump 1 0 0','turn 1 0 2 0']
    if early_ack:commands+=['take 1 0 0']
    rows=run(native,commands,policy)
    bind=[i for i,c in enumerate(commands) if c=='bind 0 0'];assert rows[bind[1]]['targets'][0][1]>rows[bind[0]]['targets'][0][1]
    pump=[i for i,c in enumerate(commands) if c=='pump 1 0 0'][-1]
    assert rows[pump]['status']==(3 if early_ack else 0)
    assert rows[-1]['callbacks'][0]==([0,0,0] if early_ack else [1,1,0]);collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_old_source_callback_during_another_backend_owner_never_touches_its_lease(native,policy):
    commands=[*ready(),'initialize','bind 0 0',*complete(),'close','pump 1 0 0','change','publish 12000000','select 1','backend 100000','open 0','bind 0 0','submit 0 1 0 1 61','dispatch','turn 2 0 2 0',*wire(0,1,[99]),'reap','close','pump 1 0 0','select 0','turn 2 0 2 0']
    rows=run(native,commands,policy);turn=commands.index('turn 2 0 2 0')
    assert rows[turn]['selected']==1 and rows[turn]['backend'][1] and rows[turn]['held']==[0,1]
    assert rows[turn]['resource']==rows[turn-1]['resource']==[12000000,2,1,7,0]
    assert rows[turn]['callbacks']==[[1,1,0],[0,0,0]] and rows[turn]['bytes'][0][0]==88
    assert rows[turn]['bytes'][1]==[-1]*64
    assert rows[-1]['callbacks']==[[1,1,0],[1,1,0]] and rows[-1]['bytes'][1][0]==99;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_counterexample_request_token_alone_does_not_identify_source_manager(native,policy):
    commands=[*ready(),'initialize',*complete(),'close','select 1','backend 400000','open 0','bind 0 0',*complete(0,1,99),'close','select 0','pump_to 1 1 0 0','turn 2 0 2 0','notice 1 0 0','select 1','notice 1 0 0','select 0','bind 0 0','pump 1 0 0','turn 2 0 2 0']
    rows=run(native,commands,policy);turn=commands.index('turn 2 0 2 0')
    # Deliberately misroute A's token to B's live callback. Both requests are
    # (generation=1,index=0,client=0); B's own result passes local validation.
    assert rows[turn]['callbacks']==[[0,0,0],[1,1,0]] and rows[turn]['bytes'][1][0]==99
    assert rows[turn]['bytes'][0]==[-1]*64 and rows[turn+1]['status']==1 and rows[turn+3]['status']==0
    assert rows[-1]['callbacks']==[[1,1,0],[1,1,0]] and rows[-1]['bytes'][0][0]==88;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_callback_turn_reentry_is_rejected_without_hidden_progress(native,policy):
    commands=[*ready(),'initialize','bind 0 0',*complete(),'close','pump 1 0 0','recurse 1','turn 2 0 2 0']
    rows=run(native,commands,policy)
    assert rows[-1]['recursive']==3 and rows[-1]['callbacks'][0]==[1,1,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('context',range(4))
def test_forbidden_or_uninitialized_turn_keeps_hint_and_buffer(native,policy,context):
    commands=[*ready(),'bind 0 0',*complete(),'close','pump 1 0 0']
    if context:commands+=['initialize']
    commands += [f'turn 1 0 2 {context}']
    rejected=len(commands)-1
    if not context:commands+=['initialize']
    commands+=['turn 1 0 2 0']
    rows=run(native,commands,policy)
    assert rows[rejected]['status']==(1 if context==0 else 3 if context==2 else 2)
    assert rows[rejected]['targets'][0][3]==1 and rows[rejected]['callbacks'][0]==[0,0,0]
    assert rows[rejected]['bytes'][0]==[-1]*64
    assert rows[-1]['callbacks'][0]==[1,1,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('flag',[0x100,0x200,0x400,0x800,0x1000])
def test_callback_preserves_error_prefix_after_peripheral_close(native,policy,flag):
    commands=[*ready(),'initialize','bind 0 0','submit 0 0 2 0 60','dispatch',*protocol.protocol.protocol.address(),'event 128 3','step',f'event {flag} {2 if flag==0x200 else 3}','step','quiet 1','step','pump 1 0 0','reap','close','pump 1 0 0','turn 1 0 2 0']
    rows=run(native,commands,policy)
    assert rows[-5]['status']==3 and rows[-4]['status']==1
    assert rows[-1]['callback_out'][0]==[2,1,0,0,flag] and rows[-1]['held']==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('end_client',[False,True])
def test_queued_cancellation_notice_does_not_require_dispatch(native,policy,end_client):
    commands=[*ready(),'initialize','bind 0 0','submit 0 0 1 0 60','cancel 1 0 0' if not end_client else 'end 0','close','pump 1 0 0','turn 1 0 2 0']
    if end_client:commands+=['take 1 0 0']
    rows=run(native,commands,policy)
    assert rows[-1]['backend'][4]==0
    assert rows[-1]['callbacks'][0]==([0,0,0] if end_client else [1,1,0])
    assert (rows[-1]['out'] if end_client else rows[-1]['callback_out'][0])==[3,0,0,0,0];collected(rows)
