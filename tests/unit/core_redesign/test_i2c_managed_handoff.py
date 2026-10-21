"""Resource lifetime through real register-engine reap and delayed user take."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;ROOT=UNIT.parents[2]
spec=importlib.util.spec_from_file_location('managed_handoff_protocol',UNIT/'test_i2c_handoff.py')
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('i2c-managed-handoff')
    original=(UNIT/'i2c_master_driver.cpp').read_text();a=original.index('struct Fake {');b=original.index('template<bool Guarded',a)
    generated=directory/'i2c_handoff_fake.inc';generated.write_text(original[a:b])
    flags=['-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    evidence=os.environ.get('EXP01_I2C_MANAGED_HANDOFF_EVIDENCE');out=Path(evidence) if evidence else None
    report=dict(compiler=compiler,compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),fake_sha256=hashlib.sha256(generated.read_bytes()).hexdigest(),inputs={},builds=[])
    if out:out.mkdir(parents=True,exist_ok=False);shutil.copy2(generated,out/generated.name)
    for path in [UNIT/'i2c_managed_handoff.hpp',UNIT/'i2c_managed_handoff_driver.cpp',UNIT/'i2c_handoff.hpp',UNIT/'i2c_handoff_driver.cpp',UNIT/'i2c_resources.hpp',UNIT/'i2c_master_driver.cpp',UNIT/'test_i2c_master.py',UNIT/'test_i2c_handoff.py',Path(__file__),ROOT/'tests/platform/core_redesign/runtime/i2c_master.hpp']:
        relative=path.relative_to(ROOT);report['inputs'][str(relative)]=hashlib.sha256(path.read_bytes()).hexdigest()
        if out:
            dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    binaries={}
    for label,source in [('managed','i2c_managed_handoff_driver.cpp'),('baseline','i2c_handoff_driver.cpp')]:
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
    binaries,evidence=native;name='baseline' if baseline else 'managed';args=[policy,mode]
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


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('tx,rx',[(0,n) for n in [1,2,3,4,8,32,64]]+[(n,0) for n in [1,2,8,32,64]]+[(2,n) for n in [1,2,3,8,32]])
def test_resource_wrapper_preserves_engine_trace_and_bytes(native,policy,tx,rx):
    suffix=[*(f'write 0 {i} {11+i}' for i in range(tx)),f'submit 0 0 {tx} {rx} 60','dispatch',*wire(tx,rx),'reap','take 1 0 0','close']
    commands=['change','publish 48000000','pins 1','backend 400000','open 0',*suffix]
    actual=run(native,commands,policy);control=run(native,['backend 1 48000000 400000','open 0',*suffix],policy,baseline=True)
    for row,old in zip(actual[3:],control):
        assert {k:row[k] for k in old}==old
    assert actual[-1]['resource']==[48000000,1,0,0,0] and actual[-1]['held']==[0,0]
    assert actual[-2]['out']==[1,tx,tx,rx,0];collected(actual)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_backend_handoff_reconfigure_and_old_take_while_new_owner_runs(native,policy):
    commands=[*ready(),'submit 0 0 0 1 60','dispatch',*wire(0,1,[88]),'close','change','select 1','backend 400000','select 0','reap','change','select 1','backend 400000','select 0','close','change','publish 12000000','select 1','backend 100000','open 1','submit 1 0 1 0 61','submit 1 1 0 1 61','dispatch','select 0','take 1 0 0','close','select 1',*wire(0,1,[99]),'reap','take 1 1 1' if policy=='dedicated' else 'take 1 0 1','close']
    rows=run(native,commands,policy)
    for i,command in enumerate(commands):
        if command=='change' and i>6 and i<commands.index('close',commands.index('reap')):assert rows[i]['status']==0
        if command=='backend 400000' and rows[i]['selected']==1:assert rows[i]['status']==2 and not rows[i]['events']
    first_close=commands.index('close');assert rows[first_close]['status']==0 and rows[first_close]['backend'][1:3]==[1,1]
    reap=commands.index('reap');assert rows[reap]['bytes'][0]==[-1]*64 and rows[reap]['held']==[1,0]
    reopened=commands.index('backend 100000');assert rows[reopened]['status']==0 and rows[reopened]['regs'][1]==12 and rows[reopened]['regs'][7]==60
    alias=commands.index('submit 1 0 1 0 61');assert rows[alias]['status']==1
    oldtake=commands.index('take 1 0 0');assert rows[oldtake]['out']==[1,0,0,1,0] and rows[oldtake]['bytes'][0][0]==88
    assert rows[oldtake]['held']==[0,1] and rows[oldtake]['resource']==[12000000,2,1,7,0]
    assert rows[oldtake+1]['status']==0 and rows[oldtake+1]['held']==[0,1]
    assert rows[-2]['out']==[1,0,0,1,0] and rows[-2]['bytes'][1][0]==99
    assert rows[-1]['held']==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('end_client',[False,True])
def test_queued_cancel_retains_resources_until_explicit_close(native,policy,end_client):
    commands=[*ready(),'submit 0 0 1 0 60','close','change','end 0' if end_client else 'cancel 1 0 0','change','close','change','publish 24000000','take 1 0 0']
    rows=run(native,commands,policy)
    assert [r['status'] for r in rows[8:14]]==[0,0,1 if end_client else 0,0,1,1]
    assert rows[10]['held']==[1,0] and rows[10]['bytes'][0]==[-1]*64
    assert rows[12]['held']==[0,0] and rows[12]['bytes'][0]==[-1]*64
    assert rows[-1]['out']==[3,0,0,0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('prefix',[[],['event 1 3','step'],['event 1 3','step','event 2 3']])
def test_running_cancel_and_client_end_cannot_release_backend(native,policy,prefix):
    commands=[*ready(),'submit 0 0 0 8 60','dispatch',*prefix,'end 0','close','change','step','close','quiet 0','step','close','take 1 0 0','reap','close','change','publish 8000000','take 1 0 0']
    rows=run(native,commands,policy)
    closes=[i for i,c in enumerate(commands) if c=='close']
    assert [rows[i]['status'] for i in closes]==[0,0,0,1]
    assert all(rows[i]['held']==[1,0] for i in closes[:-1])
    assert rows[-7]['status']==0 and rows[-7]['bytes'][0]==[-1]*64
    assert rows[-1]['out']==[3,0,0,0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('mask',[0,1,2,4,7])
def test_other_logical_leases_block_pins_or_clock_as_declared(native,policy,mask):
    commands=['change','publish 48000000','pins 1',f'claim {mask}','backend 400000','change']
    if mask:commands+=['release','backend 400000','close','change']
    else:commands+=['close','change','release','change']
    rows=run(native,commands,policy)
    assert rows[4]['status']==(2 if mask else 0)
    assert rows[5]['status']==0
    if mask:
        assert rows[4]['pin_checks']==0 and rows[4]['events']==[]
        assert rows[7]['status']==0 and rows[-1]['status']==1
    else:
        assert rows[4]['resource'][2:4]==[2,7]
        assert rows[6]['resource'][2:4]==[1,0] and rows[7]['status']==0 and rows[-1]['status']==1
    collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_open_failures_roll_back_only_their_lease(native,policy):
    commands=['backend 400000','change','backend 400000','publish 48000000','backend 400000','pins 1','backend 123456','control 1','backend 400000','control 0','event 0 2','backend 400000','quiet 0','backend 400000','backend 100000','select 1','backend 100000','select 0','close','backend 100000','close']
    rows=run(native,commands,policy)
    assert [rows[i]['status'] for i in [0,2,4,6,8,11,13,14,16,18,19,20]]==[2,2,3,5,4,4,0,1,2,1,0,1]
    for i in [0,2,4,6,8,11]:assert rows[i]['resource'][2:4]==[0,0] and rows[i]['held']==[0,0]
    for i in [0,2,4,6]:assert rows[i]['events']==[]
    assert all(event[0]!=1 for i in [8,11] for event in rows[i]['events'])
    assert rows[14]['pin_checks']==rows[13]['pin_checks']==rows[16]['pin_checks']
    assert rows[-1]['held']==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_idle_open_holds_clock_and_pins_until_close(native,policy):
    commands=[*ready(),'change','select 1','backend 400000','select 0','close','change','publish 2000000','backend 400000','backend 100000','close']
    rows=run(native,commands,policy)
    assert rows[7]['status']==0 and rows[9]['status']==2 and rows[13]['status']==1
    assert rows[14]['status']==5 and rows[14]['resource'][2]==0 and not rows[14]['events']
    assert rows[15]['status']==0 and rows[15]['regs'][1]==2
    assert rows[-1]['held']==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_domain_ticket_exhaustion_does_not_wrap_or_touch_mmio(native,policy):
    commands=['change','publish 48000000','pins 1']
    for _ in range(255):commands+=['backend 400000','close']
    commands+=['backend 400000','select 1','backend 400000','change','publish 12000000','backend 100000']
    rows=run(native,commands,policy,'domain8')
    assert all(rows[3+2*i]['status']==0 and rows[4+2*i]['status']==1 for i in range(255))
    assert rows[-6]['status']==rows[-4]['status']==rows[-1]['status']==2
    assert all(not row['events'] for row in rows[-6:]) and rows[-1]['held']==[0,0]
    assert rows[-1]['pin_checks']==255;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
def test_domain_clock_epoch_exhaustion_preserves_last_published_clock(native,policy):
    commands=[]
    for _ in range(255):commands+=['change','publish 48000000']
    commands+=['change','publish 12000000','pins 1','backend 400000','close']
    rows=run(native,commands,policy,'domain8')
    assert rows[-5]['status']==rows[-4]['status']==0
    assert rows[-5]['resource']==rows[-4]['resource']==[48000000,255,0,0,0]
    assert rows[-2]['status']==0 and rows[-1]['status']==1;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('flag',[0x100,0x200,0x400,0x800,0x1000])
def test_fault_reap_and_close_keep_old_prefix_without_releasing_new_owner(native,policy,flag):
    commands=[*ready(),'submit 0 0 2 0 60','dispatch',*protocol.protocol.address(),'event 128 3','step',f'event {flag} {2 if flag==0x200 else 3}','step','quiet 1','step','close','reap','change','close','claim 7','take 1 0 0','change','release','change']
    rows=run(native,commands,policy)
    assert rows[-9]['status']==0 and rows[-9]['held']==[1,0]
    assert rows[-8]['status']==1 and rows[-8]['held']==[1,0]
    assert rows[-7]['status']==0 and rows[-6]['status']==1
    assert rows[-5]['status']==1 and rows[-4]['out']==[2,1,0,0,flag]
    assert rows[-4]['resource'][2:4]==[1,7] and rows[-4]['held']==[0,0]
    assert rows[-3]['status']==0 and rows[-1]['status']==1;collected(rows)
