"""Resource retention and rollback around the existing register state machine."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;REPO=UNIT.parents[2]
INPUT=REPO/'planning/core-redesign/i2c-resource-inputs.json'
CONFIG=json.loads(INPUT.read_text())
spec=importlib.util.spec_from_file_location('i2c_protocol_helpers',UNIT/'test_i2c_master.py')
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    root=tmp_path_factory.mktemp('i2c-resources')
    original=(UNIT/'i2c_master_driver.cpp').read_text()
    begin=original.index('struct Fake {');end=original.index('template<bool Guarded',begin)
    generated=root/'i2c_resource_fake.inc';generated.write_text(original[begin:end])
    flags=['-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    out=None
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    paths=[UNIT/n for n in ['i2c_resources.hpp','i2c_resources_driver.cpp','i2c_master_driver.cpp',
                           'test_i2c_master.py',Path(__file__).name]]
    paths += [INPUT,REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp']
    report=dict(compiler=compiler,compiler_sha256=sha(compiler),fake_sha256=sha(generated),
                inputs={str(p.relative_to(REPO)):sha(p) for p in paths},builds=[])
    if destination:=os.environ.get('EXP01_I2C_RESOURCE_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False);shutil.copy2(generated,out/generated.name)
        for path in paths:
            dest=out/'inputs'/path.relative_to(REPO);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    binaries={}
    for name,source in [('managed','i2c_resources_driver.cpp'),('baseline','i2c_master_driver.cpp')]:
        exe=root/name
        command=[compiler,*flags,'-I',str(root),str(UNIT/source),'-o',str(exe)]
        p=subprocess.run(command,capture_output=True,text=True,timeout=180)
        item=dict(name=name,command=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        report['builds'].append(item)
        if out:(out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
        assert p.returncode==0,p.stderr
        item['binary_sha256']=sha(exe)
        if out:
            shutil.copy2(exe,out/name);(out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
        binaries[name]=exe
    return binaries


def replay(native,commands,short=False,baseline=False):
    args=[str(native['baseline' if baseline else 'managed'])]
    args += ['guarded','normal'] if baseline else ['short' if short else 'normal']
    stdin='\n'.join(commands)+'\n'
    p=subprocess.run(args,input=stdin,capture_output=True,text=True,timeout=60)
    if destination:=os.environ.get('EXP01_I2C_RESOURCE_EVIDENCE'):
        key=hashlib.sha256(json.dumps([args[0].split('/')[-1],args[1:],stdin]).encode()).hexdigest()
        path=Path(destination)/f'trace-{key}.json'
        record=dict(program='baseline' if baseline else 'managed',args=args[1:],stdin=stdin,
                    returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        if path.exists():assert json.loads(path.read_text())==record
        else:path.write_text(json.dumps(record,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];assert len(rows)==len(commands)
    if not baseline:
        for row in rows:
            if any(row['held']):assert row['domain'][2]>=1 and row['domain'][3]&7==7
            if row['active']:assert row['held'][row['selected']] and not row['available']
            if row['domain'][4]:assert not row['domain'][0] and not row['domain'][2]
    return rows


def ready(hz=48000000):return ['change',f'publish {hz}','pins 1']


def test_open_rollback_and_no_clock_change_or_pin_reconfiguration_while_open(native):
    commands=['open 100000','change','publish 48000001','open 100000','publish 48000000',
              'open 100000','pins 1','open 100001','control 1','open 100000','control 0',
              'open 400000','change','pins 0','select 1','open 100000','select 0','end','change','publish 12000000']
    rows=replay(native,commands)
    assert [rows[i]['status'] for i in [0,1,2,3,4,5,6,7,9,11]]==[2,1,0,2,1,3,1,5,4,0]
    for i in [0,3,5,7]:assert rows[i]['domain'][2:4]==[0,0] and not rows[i]['events']
    assert not rows[12]['status'] and not rows[13]['status'] and rows[15]['status']==2
    assert rows[17]['held']==[0,0] and rows[17]['domain'][2]==0
    assert rows[-1]['domain']==[12000000,2,0,0,0]


@pytest.mark.parametrize('hz',[2000000,4000000,12000000,48000000])
@pytest.mark.parametrize('rate',[100000,400000])
def test_pclk_declaration_is_used_only_after_transition_and_failed_open_returns_claim(native,hz,rate):
    rows=replay(native,[*ready(hz),f'open {rate}'])
    if rate==400000 and hz<4000000:
        assert rows[-1]['status']==5 and rows[-1]['domain'][2:4]==[0,0] and not rows[-1]['events']
    else:
        assert rows[-1]['status']==0 and rows[-1]['domain'][2:4]==[1,7]
        divisor=rate*(3 if rate==400000 else 2)
        assert rows[-1]['regs'][1]==hz//1000000
        assert rows[-1]['regs'][7]==((hz+divisor-1)//divisor)|(0x8000 if rate==400000 else 0)


@pytest.mark.parametrize('close_before_collection',[False,True])
def test_notification_and_hardware_terminal_do_not_release_clock_or_pins(native,close_before_collection):
    commands=[*ready(),'open 100000','start 1 0 60',*protocol.address(),*protocol.transmit(1),'quiet 0','step','peek','ack 1','change','pins 0']
    if close_before_collection:commands+=['end','change','take 1','change']
    else:commands+=['take 1','change','end','change']
    rows=replay(native,commands)
    assert rows[-7]['terminal'] and rows[-7]['held']==[1,0]
    if close_before_collection:
        assert rows[-4]['held']==[1,0] and not rows[-3]['status']
    else:assert rows[-4]['held']==[1,0] and not rows[-3]['status']
    assert rows[-1]['status']==1 and rows[-1]['held']==[0,0] and rows[-1]['domain'][4]


@pytest.mark.parametrize('length',[1,2,3,8,32])
def test_end_during_receive_keeps_resources_until_local_stop_and_collection(native,length):
    commands=[*ready(),'open 400000',f'start 0 {length} 60',*protocol.address(True),'end','change','pins 0',
              *protocol.receive([100+i for i in range(length)]),'quiet 0','step','change','take 1','change']
    rows=replay(native,commands)
    assert not rows[-3]['status'] and rows[-3]['held']==[1,0] and rows[-3]['terminal']
    assert rows[-2]['out']==[1,0,0,length,0] and rows[-2]['held']==[0,0]
    assert rows[-1]['status']==1


@pytest.mark.parametrize('flag',[0x100,0x200,0x400,0x800,0x1000])
def test_error_and_uncollected_end_keep_lease(native,flag):
    commands=[*ready(),'open 100000','start 2 0 60',*protocol.address(),'event 128 3','step',
              f'event {flag} {2 if flag==0x200 else 3}','step','end','change','quiet 1','step','change',
              'take 0','change','take 1','change']
    rows=replay(native,commands)
    assert not rows[-3]['status'] and rows[-3]['held']==[1,0]
    assert rows[-2]['out']==[2,1,0,0,flag] and rows[-2]['held']==[0,0]
    assert rows[-1]['status']==1


def test_conflicting_pin_and_clock_only_clients_capacity_and_stale_release(native):
    commands=[*ready(),'claim 0 2','open 100000','claim 1 0','change','release 0 1 0',
              'open 100000','release 0 1 0','end','change','release 1 2 1','change']
    rows=replay(native,commands)
    assert rows[4]['status']==2 and rows[8]['status']==0
    assert rows[8]['domain'][2:4]==[2,7]
    assert rows[9]['status']==0 and rows[9]['held']==[1,0]
    assert rows[10]['domain'][2:4]==[1,0] and not rows[11]['status']
    assert rows[-1]['status']==1


def test_two_clock_only_clients_can_fill_the_table_without_holding_pins(native):
    rows=replay(native,[*ready(),'claim 0 0','claim 1 0','open 100000','release 0 1 0','open 100000','end','change'])
    assert rows[5]['status']==2 and rows[5]['domain'][2:4]==[2,0] and not rows[5]['events']
    assert rows[7]['status']==0 and rows[7]['domain'][2:4]==[2,7]
    assert rows[8]['domain'][2:4]==[1,0] and not rows[9]['status']


@pytest.mark.parametrize('phase',['start','stop'])
def test_stalled_controller_never_releases_resources_just_because_polling_repeats(native,phase):
    commands=[*ready(),'open 100000','start 1 0 60']
    if phase=='stop':commands+=['event 1 3','step','cancel 1','step','end']
    for _ in range(100):commands+=['step','change','pins 0']
    stalled=len(commands)-1
    commands+=['end','quiet 0','step','step','take 1','change']
    rows=replay(native,commands)
    assert rows[stalled]['active'] and not rows[stalled]['terminal'] and rows[stalled]['held']==[1,0]
    assert rows[stalled]['domain']==[48000000,1,1,7,0]
    assert rows[-2]['out'][0]==3 and rows[-2]['held']==[0,0] and rows[-1]['status']==1


@pytest.mark.parametrize('kind',['read','write','combined'])
@pytest.mark.parametrize('seed',CONFIG['seeds'])
def test_exact_i2c_transcript_matches_unwrapped_engine(native,kind,seed):
    rng=random.Random(seed+0x12c);n=rng.choice([1,2,3,8,32]);payload=[rng.randrange(256) for _ in range(n)]
    tx=[rng.randrange(256) for _ in range(2)] if kind=='combined' else payload if kind=='write' else []
    rx=payload if kind!='write' else []
    common=[*[f'write {i} {v}' for i,v in enumerate(tx)],f'start {len(tx)} {len(rx)} 60']
    common+=protocol.address(not tx)
    if tx:common+=protocol.transmit(len(tx))
    if tx and rx:common+=protocol.address(True)
    if rx:common+=protocol.receive(rx)
    common+=['quiet 0','step','peek','ack 1','take 1']
    managed=replay(native,[*ready(),'open 400000',*common])[4:]
    baseline=replay(native,['open 1 48000000 400000',*common],baseline=True)[1:]
    fields=['status','open','active','phase','terminal','token','available','result','issued','confirmed','received',
            'errors','notice','value','out','regs','bytes','tx','addr_ctl','events']
    assert [{k:r[k] for k in fields} for r in managed]==[{k:r[k] for k in fields} for r in baseline]


@pytest.mark.parametrize('kind',['generation','epoch'])
def test_resource_counters_exhaust_without_reusing_old_tickets(native,kind):
    if kind=='generation':
        commands=ready()
        for g in range(1,256):commands += ['claim 0 0',f'release 0 {g} 0']
        commands+=['claim 0 0','change','publish 12000000','claim 0 0','open 100000']
        rows=replay(native,commands,short=True)
        assert not rows[-5]['status'] and not rows[-2]['status'] and rows[-1]['status']==2
        assert rows[-1]['ticket']==[255,0]
    else:
        commands=['change','publish 12000000']*255+['change','publish 48000000']
        rows=replay(native,commands,short=True)
        assert not rows[-2]['status'] and not rows[-1]['status'] and rows[-1]['domain']==[12000000,255,0,0,0]


@pytest.mark.parametrize('short',[False,True])
@pytest.mark.parametrize('seed',CONFIG['seeds'])
def test_generated_ledger_matches_independent_owner_list_without_mmio(native,short,seed):
    rng=random.Random(0xc10c+seed);limit=255 if short else 0xffffffff
    leases=[None,None];generation=epoch=frequency=0;changing=False;prepared=False;ticket=[99,99]
    commands=[];expected=[]
    for _ in range(CONFIG['ledger_operations']):
        choice=rng.randrange(5);held=0
        for lease in leases:
            if lease:held|=lease[2]
        ok=False
        if choice==0:
            command='change'
            if not changing and not any(leases) and epoch<limit:changing=True;frequency=0;ok=True
        elif choice==1:
            hz=rng.choice([0,2000000,4000000,12000000,48000000,48000001]);command=f'publish {hz}'
            if changing and 2000000<=hz<=48000000 and hz%1000000==0:
                frequency=hz;epoch+=1;changing=False;ok=True
        elif choice==2:
            owner=rng.randrange(2);mask=rng.randrange(9);command=f'claim {owner} {mask}'
            if mask<=7 and frequency and not changing and not held&mask and generation<limit and not any(l and l[0]==owner for l in leases):
                free=next((i for i,l in enumerate(leases) if not l),None)
                if free is not None:
                    generation+=1;leases[free]=[owner,generation,mask];ticket=[generation,free];ok=True
        elif choice==3:
            index=rng.randrange(3);lease=leases[index] if index<2 else None
            owner=lease[0] if lease and rng.randrange(4) else rng.randrange(2)
            g=lease[1] if lease and rng.randrange(4) else rng.randrange(generation+2)
            command=f'release {owner} {g} {index}'
            if lease and g and lease[:2]==[owner,g]:leases[index]=None;ok=True
        else:
            value=rng.randrange(2);command=f'pins {value}'
            if not held&6:prepared=bool(value);ok=True
        held=0
        for lease in leases:
            if lease:held|=lease[2]
        commands.append(command)
        expected.append(dict(status=int(ok),domain=[frequency,epoch,sum(bool(l) for l in leases),held,int(changing)],
                             prepared=int(prepared),ticket=list(ticket)))
    rows=replay(native,commands,short=short)
    for command,row,wanted in zip(commands,rows,expected):
        assert {k:row[k] for k in wanted}==wanted,command
        assert not row['events'] and row['held']==[0,0] and row['available']
