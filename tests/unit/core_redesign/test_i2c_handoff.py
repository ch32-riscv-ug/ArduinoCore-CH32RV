"""Unchanged register engine, split backend/outer loans and retained results."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;ROOT=UNIT.parents[2]
spec=importlib.util.spec_from_file_location('handoff_protocol',UNIT/'test_i2c_master.py')
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('i2c-handoff')
    original=(UNIT/'i2c_master_driver.cpp').read_text();a=original.index('struct Fake {');b=original.index('template<bool Guarded',a)
    generated=directory/'i2c_handoff_fake.inc';generated.write_text(original[a:b])
    flags=['-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    evidence=os.environ.get('EXP01_I2C_HANDOFF_EVIDENCE');out=Path(evidence) if evidence else None
    report=dict(compiler=compiler,compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),fake_sha256=hashlib.sha256(generated.read_bytes()).hexdigest(),inputs={},builds=[])
    if out:out.mkdir(parents=True,exist_ok=False);shutil.copy2(generated,out/generated.name)
    for path in [UNIT/'i2c_handoff.hpp',UNIT/'i2c_handoff_driver.cpp',UNIT/'i2c_master_driver.cpp',UNIT/'test_i2c_master.py',Path(__file__),ROOT/'tests/platform/core_redesign/runtime/i2c_master.hpp']:
        relative=path.relative_to(ROOT);report['inputs'][str(relative)]=hashlib.sha256(path.read_bytes()).hexdigest()
        if out:
            dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    binaries={}
    for label,source in [('handoff','i2c_handoff_driver.cpp'),('baseline','i2c_master_driver.cpp')]:
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
    binaries,evidence=native;name='baseline' if baseline else 'handoff';args=['guarded','normal'] if baseline else [policy,mode]
    stdin='\n'.join(commands)+'\n';result=subprocess.run([str(binaries[name]),*args],input=stdin,capture_output=True,text=True,timeout=60)
    if evidence:
        key=hashlib.sha256(json.dumps([name,args,stdin]).encode()).hexdigest()
        (evidence/(key+'.json')).write_text(json.dumps(dict(program=name,args=args,stdin=stdin,stdout=result.stdout,stderr=result.stderr,returncode=result.returncode),indent=2)+'\n')
    assert result.returncode==0,result.stderr;assert not result.stderr
    rows=[json.loads(line) for line in result.stdout.splitlines()];assert len(rows)==len(commands)
    if not baseline:
        for command,row in zip(commands,rows):
            if command.split()[0] not in ['backend','close','dispatch','step']:assert row['events']==[],(command,row)
            if row['backend'][1]:assert row['current']<len(row['records']) and row['records'][row['current']][0]==2
            if row['current']==len(row['records']):assert not row['backend'][1]
    return rows


def ready():return ['backend 1 48000000 400000','open 0','open 1','open 2']
def wire(tx,rx,values=None):
    values=list(range(80,80+rx)) if values is None else values
    commands=protocol.address(tx==0)
    if tx:commands+=protocol.transmit(tx)
    if tx and rx:commands+=protocol.address(True)
    if rx:commands+=protocol.receive(values)
    return [*commands,'quiet 0','step']
def collected(rows):assert all(all(value>=0 for value in b) for b in rows[-1]['bytes'])


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('tx,rx',[(0,n) for n in [1,2,3,4,8,32,64]]+[(n,0) for n in [1,2,8,32,64]]+[(2,n) for n in [1,2,3,8,32]])
def test_wire_trace_and_bytes_equal_original_engine(native,policy,tx,rx):
    data=[11+i for i in range(tx)];commands=[];baseline=[];pairs=[]
    def pair(new,old=None):
        commands.append(new)
        if old is not None:baseline.append(old);pairs.append((len(commands)-1,len(baseline)-1))
    pair('backend 1 48000000 400000','open 1 48000000 400000');pair('open 0')
    for i,v in enumerate(data):pair(f'write 0 {i} {v}',f'write {i} {v}')
    pair(f'submit 0 0 {tx} {rx} 60');submit_index=len(commands)-1
    pair('dispatch',f'start {tx} {rx} 60')
    for command in wire(tx,rx):pair(command,command)
    pair('take 1 0 0');premature=len(commands)-1
    pair('notice 1 0 0');pair('reap','take 1');reaped=len(commands)-1
    pair('take 1 0 0')
    actual=run(native,commands,policy);control=run(native,baseline,baseline=True)
    assert actual[submit_index]['status']==0 and actual[submit_index]['bytes'][0]==[-1]*64
    assert actual[premature]['status']==0 and actual[premature]['backend'][1:3]==[1,1]
    assert actual[reaped]['status']==1 and actual[reaped]['backend'][1:3]==[0,0] and actual[reaped]['bytes'][0]==[-1]*64
    for i,j in pairs:
        for field in ['events','regs','tx','addr_ctl']:assert actual[i][field]==control[j][field],(commands[i],field)
    assert actual[-1]['out']==control[-1]['out']==[1,tx,tx,rx,0]
    assert actual[-1]['bytes'][0]==control[-1]['bytes'];collected(actual)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_next_bus_owner_runs_while_first_result_and_buffer_stay_held(native,policy):
    commands=[*ready(),'write 0 0 17','submit 0 0 1 0 60','submit 1 1 0 2 61','submit 2 2 1 0 62','dispatch',*wire(1,0),'reap','dispatch',*wire(0,2,[88,89]),'reap','take 1 1 1','ack 1 0 0','notice 1 0 0','take 1 0 0']
    if policy=='dedicated':commands+=['cancel 1 2 2','take 1 2 2']
    rows=run(native,commands,policy);second=commands.index('dispatch',commands.index('dispatch')+1)
    assert rows[7]['status']==(0 if policy=='dedicated' else 1)
    assert rows[second]['status']==0 and rows[second]['token']==[1,1,1]
    assert rows[second]['bytes'][0]==[-1]*64 and rows[second]['backend'][1]
    take=commands.index('take 1 1 1');assert rows[take]['out']==[1,0,0,2,0] and rows[take]['bytes'][1][:2]==[88,89]
    assert rows[take]['bytes'][0]==[-1]*64
    assert rows[take+2]['status']==0 and rows[take+3]['out']==[1,1,1,0,0]
    collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('close_client',[False,True])
def test_queued_cancel_or_end_never_uses_backend_and_keeps_outer_loan(native,policy,close_client):
    action='end 0' if close_client else 'cancel 1 0 0'
    commands=[*ready(),'submit 0 0 0 8 60','close',action,'notice 1 0 0','open 0','write 0 0 99','close','take 1 0 0']
    rows=run(native,commands,policy)
    assert rows[5]['status']==0
    assert rows[6]['events']==[] and rows[6]['records'][0][0]==3 and rows[6]['backend'][1]==0
    assert rows[7]['status']==int(not close_client) and rows[8]['status']==0 and rows[9]['status']==0
    assert rows[-2]['status']==1 and rows[-2]['bytes'][0]==[-1]*64
    assert rows[-1]['out']==[3,0,0,0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('prefix',[[],['event 1 3','step'],['event 1 3','step','event 2 3']])
def test_running_cancel_waits_for_stop_and_explicit_reap(native,policy,prefix):
    commands=[*ready(),'submit 0 0 0 8 60','dispatch',*prefix,'cancel 1 0 0','take 1 0 0','step','quiet 0','step','take 1 0 0','reap','take 1 0 0']
    rows=run(native,commands,policy)
    assert rows[-3]['status']==0 and rows[-3]['bytes'][0]==[-1]*64
    assert rows[-2]['status']==1 and rows[-2]['bytes'][0]==[-1]*64
    assert rows[-1]['out']==[3,0,0,0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_end_during_read_can_complete_success_without_notice(native,policy):
    commands=[*ready(),'submit 0 0 0 3 60','dispatch',*protocol.address(True),'end 0','open 0',*protocol.receive([1,2,3]),'quiet 0','step','reap','notice 1 0 0','take 1 0 0','open 0']
    rows=run(native,commands,policy)
    assert rows[-3]['status']==0 and rows[-2]['out']==[1,0,0,3,0] and rows[-1]['status']==1
    collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('flag',[0x100,0x200,0x400,0x800,0x1000])
def test_error_prefix_and_other_master_busy_are_preserved(native,policy,flag):
    commands=[*ready(),'submit 0 0 2 0 60','dispatch',*protocol.address(),'event 128 3','step',f'event {flag} {2 if flag==0x200 else 3}','step','quiet 1','step','reap','submit 1 1 1 0 61','dispatch','take 1 0 0','cancel 1 1 1','take 1 1 1']
    rows=run(native,commands,policy)
    assert rows[-4]['status']==1 and rows[-4]['records'][1][0]==1 and rows[-4]['backend'][1]==0
    assert rows[-3]['out']==[2,1,0,0,flag]
    if flag==0x200:assert all(not(e[:2]==[1,0] and e[2]&0x200) for row in rows[12:] for e in row['events'])
    collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_invalid_inputs_duplicate_buffer_stale_tokens_and_output_preservation(native,policy):
    commands=[*ready(),'submit 0 3 1 0 60','submit 0 0 0 0 60','submit 0 0 64 1 60','submit 0 0 1 0 128','submit 0 0 1 0 0','submit 0 0 1 0 120','submit 0 0 1 0 60','submit 1 0 1 0 61','cancel 0 0 0','take 1 1 0','cancel 1 0 1','cancel 1 0 0','take 1 0 0','submit 0 0 1 0 60','take 1 0 0','cancel 1 0 0','cancel 2 0 0','take 2 0 0']
    rows=run(native,commands,policy)
    assert [r['status'] for r in rows[4:10]]==[2,2,2,2,3,3]
    assert all(r['token']==[99,99,99] for r in rows[4:10])
    assert rows[11]['status']==1 and rows[11]['token']==[1,0,0]
    assert rows[-4]['status']==0 and rows[-3]['status']==3
    assert rows[-1]['token']==[2,0,0] and rows[-1]['out']==[3,0,0,0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_request_generation_retirement_does_not_consume_backend_generation(native,policy):
    commands=ready();count=255 if policy=='dedicated' else 510
    for i in range(count):
        generation=i%255+1;index=0 if i<255 else 1
        commands += ['submit 0 0 1 0 60',f'cancel {generation} {index} 0',f'take {generation} {index} 0']
    commands+=['submit 0 0 1 0 60','take 1 0 0']
    rows=run(native,commands,policy,'request8')
    assert rows[-2]['status']==5 and rows[-1]['status']==0 and rows[-1]['backend'][4]==0
    assert all(not row['events'] for row in rows[1:]);collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_backend_generation_exhaustion_leaves_accepted_request_cancelable(native,policy):
    commands=ready()
    for i in range(1,256):commands += ['submit 0 0 1 0 60','dispatch',*wire(1,0),'reap',f'take {i} 0 0']
    generation,index=(1,1) if policy=='dedicated' else (256,0)
    commands+=['close','backend 1 48000000 400000','submit 1 1 1 0 61','dispatch',f'cancel {generation} {index} 1',f'take {generation} {index} 1']
    rows=run(native,commands,policy,'backend8')
    assert rows[-3]['status']==4 and rows[-3]['events']==[] and rows[-3]['backend'][4]==255
    assert rows[-1]['out']==[3,0,0,0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_round_robin_after_collection_and_immediate_resubmission(native,policy):
    clients=3 if policy=='dedicated' else 2;commands=ready()
    for c in range(clients):commands.append(f'submit {c} {c} 1 0 {60+c}')
    dispatches=[]
    for _ in range(2):
        for c in range(clients):
            dispatches.append(len(commands));commands += ['dispatch',*wire(1,0),'reap']
            generation=1+len(dispatches[:-1])//clients
            commands += [f'take {generation} {c} {c}',f'submit {c} {c} 1 0 {60+c}']
    for c in range(clients):commands += [f'end {c}',f'take 3 {c} {c}']
    rows=run(native,commands,policy)
    assert [rows[i]['token'][2] for i in dispatches]==list(range(clients))*2
    assert all(rows[i]['status']==0 for i in dispatches);collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_wire_busy_dispatch_keeps_reservation_without_consuming_backend_generation(native,policy):
    commands=[*ready(),'submit 0 0 1 0 60','event 0 2','dispatch','dispatch','close','quiet 0','dispatch',*wire(1,0),'reap','take 1 0 0']
    rows=run(native,commands,policy)
    for i in [6,7]:
        assert rows[i]['status']==1 and rows[i]['records'][0][0]==1 and rows[i]['backend'][1:]==[0,0,0,0]
        assert rows[i]['token']==[1,0,0] and rows[i]['bytes'][0]==[-1]*64
    assert rows[8]['status']==0 and rows[10]['status']==0 and rows[10]['backend'][4]==1;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_backend_can_close_and_reconfigure_with_only_outer_result_held(native,policy):
    commands=[*ready(),'submit 0 0 0 1 60','dispatch',*wire(0,1,[88]),'reap','close','backend 1 12000000 100000','notice 1 0 0','take 1 0 0','submit 0 0 0 1 60','dispatch',*wire(0,1,[99]),'reap','take 2 0 0']
    rows=run(native,commands,policy);closed=commands.index('close')
    assert rows[closed]['status']==1 and rows[closed]['bytes'][0]==[-1]*64
    assert rows[closed+1]['status']==0 and rows[closed+2]['status']==1
    assert rows[closed+3]['out']==[1,0,0,1,0] and rows[closed+3]['bytes'][0][0]==88
    assert rows[-1]['backend'][4]==2 and rows[-1]['bytes'][0][0]==99;collected(rows)
