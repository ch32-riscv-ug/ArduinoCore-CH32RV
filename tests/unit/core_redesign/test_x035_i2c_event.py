"""Frozen X035 setup/route adapter composed with managed completion delivery."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;ROOT=UNIT.parents[2]
INPUT=ROOT/'planning/core-redesign/x035-i2c-event-inputs.json'
ROUTES=ROOT/'planning/core-redesign/x035-i2c-route-inputs.json'
CONFIG=json.loads(ROUTES.read_text())
AVAILABLE=[(p['id'],r) for p in CONFIG['packages'] for r in p['routes']]
spec=importlib.util.spec_from_file_location('x035_i2c_event_wire',UNIT/'test_i2c_master.py')
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)


def generated_fakes():
    original=(UNIT/'i2c_master_driver.cpp').read_text()
    engine=original[original.index('struct Fake {'):original.index('template<bool Guarded')]
    board=(UNIT/'x035_setup_fake.hpp').read_text().replace('words[6]={3,0,0,0,0,0}','words[7]={3,0,0,0,0,0,0}')
    board=board.replace('Remap,PinConfig};','Remap,PinConfig,0x4001101cu};').replace('i<6','i<7')
    board=board.replace('if(i==5)assert(words[2]&4);','if(i==5)assert(words[2]&4);\n        if(i==6)assert(words[2]&16);')
    return {'i2c_handoff_fake.inc':engine,'x035_route_fake.inc':board}


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('x035-i2c-event');out=None
    if destination:=os.environ.get('EXP03_X035_I2C_EVENT_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    generated={}
    for name,source in generated_fakes().items():
        path=directory/name;path.write_text(source);generated[name]=sha(path)
        if out:shutil.copy2(path,out/name)
    flags=['-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    names=['x035_i2c_event_driver.cpp','test_x035_i2c_event.py','x035_i2c_routes.hpp','x035_i2c_setup.hpp','x035_setup_fake.hpp',
           'completion_delivery.hpp','event_executor.hpp','work_executor.hpp','data_parts.hpp','i2c_managed_handoff.hpp',
           'i2c_handoff.hpp','i2c_resources.hpp','i2c_master_driver.cpp','test_i2c_master.py']
    paths=[*(UNIT/name for name in names),INPUT,ROUTES,ROOT/'tests/platform/core_redesign/runtime/i2c_master.hpp']
    report=dict(compiler=compiler,compiler_sha256=sha(Path(compiler)),generated=generated,inputs={},builds=[])
    for path in paths:
        relative=path.relative_to(ROOT);report['inputs'][str(relative)]=sha(path)
        if out:
            dest=out/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    def build(package,route):
        label=f'p{package}-r{route}';exe=directory/label
        command=[compiler,*flags,'-I',str(directory),f'-DPACKAGE_ID={package}',f'-DROUTE={route}',str(UNIT/'x035_i2c_event_driver.cpp'),'-o',str(exe)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=180)
        item=dict(name=label,command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr)
        if result.returncode==0:
            item['executable_sha256']=sha(exe)
            if out:shutil.copy2(exe,out/label)
        return (package,route),exe,item
    binaries={}
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures=[pool.submit(build,p,r) for p in range(2) for r in range(8)]
        for future in as_completed(futures):
            key,exe,item=future.result();report['builds'].append(item);report['builds'].sort(key=lambda b:b['name'])
            if out:(out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
            assert item['returncode']==0,item['stderr']
            binaries[key]=exe
    return binaries,out


def run(native,commands,policy='dedicated',package=0,route=0):
    binaries,out=native;exe=binaries[package,route];stdin='\n'.join(commands)+'\n'
    result=subprocess.run([str(exe),policy],input=stdin,capture_output=True,text=True,timeout=60)
    if out:
        key=hashlib.sha256(json.dumps([exe.name,policy,stdin]).encode()).hexdigest()
        (out/(key+'.json')).write_text(json.dumps(dict(program=exe.name,args=[policy],stdin=stdin,stdout=result.stdout,stderr=result.stderr,returncode=result.returncode),indent=2)+'\n')
    assert result.returncode==0,result.stderr;assert not result.stderr
    rows=[json.loads(line) for line in result.stdout.splitlines()];assert len(rows)==len(commands)
    for command,row in zip(commands,rows):
        op=command.split()[0]
        if op not in ['prepare','ready','backend','close','dispatch','step']:assert row['events']==[],(command,row)
        if op not in ['prepare','ready','pclk','backend']:assert row['board_events']==[],(command,row)
        if row['setup_held']:assert row['resource'][2:4]==[1,7] and not any(row['held'])
        if any(row['held']):assert row['resource'][2:4]==[1,7] and not row['setup_held']
        if row['backend'][0]:assert row['held'][row['selected']]
    return rows


def prepared():return ['change','publish 48000000','set32 4 67108864','prepare 1']
def ready():return [*prepared(),'backend 400000','open 0','open 1','open 2','initialize','bind 0 0']
def wire(tx,rx,values):
    commands=protocol.address(tx==0)
    if tx:commands+=protocol.transmit(tx)
    if tx and rx:commands+=protocol.address(True)
    if rx:commands+=protocol.receive(values)
    return [*commands,'quiet 0','step']
def complete(client=0,buffer=0,value=88):
    return [f'submit {client} {buffer} 0 1 {60+client}','dispatch',*wire(0,1,[value]),'reap']
def collected(rows):assert all(all(value>=0 for value in b) for b in rows[-1]['bytes'])


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('package,route',AVAILABLE)
@pytest.mark.parametrize('tx,rx',[(0,1),(0,2),(0,3),(2,3)])
def test_prepared_routes_preserve_fields_and_deliver_original_buffer(native,policy,package,route,tx,rx):
    original_remap=0xb4f0f0ff;initial_a=0xa5a5a5a5;initial_c=0x5a5a5a5a
    commands=['change','publish 48000000','set32 2 160',f'set32 4 {original_remap}',f'set32 5 {initial_a}',f'set32 6 {initial_c}','prepare 1','backend 400000','open 0','initialize','bind 0 0',f'submit 0 0 {tx} {rx} 60','dispatch','prepare 1',*wire(tx,rx,list(range(88,88+rx))),'reap','close','pump 1 0 0','turn 1 0 2 0']
    rows=run(native,commands,policy,package,route);mapping=CONFIG['routes'][route];index=5 if route<2 else 6;other=11-index
    assert rows[6]['status']==0 and rows[6]['resource'][2:4]==[0,0]
    assert rows[6]['board'][2]==160|mapping['gate_apb2'] and rows[6]['board'][4]==(original_remap&~0x1c)|(route<<2)
    initial=initial_a if index==5 else initial_c
    assert rows[6]['board'][index]==(initial&~mapping['mask'])|mapping['mode']
    assert rows[6]['board'][other]==(initial_c if other==6 else initial_a)
    assert rows[7]['status']==0 and rows[13]['status']==2 and not rows[13]['events'] and not rows[13]['board_events']
    assert rows[-1]['callback_out'][0]==[1,tx,tx,rx,0]
    assert rows[-1]['bytes'][0][tx:tx+rx]==list(range(88,88+rx));collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('route',[0,1])
def test_unsupported_f7_route_never_programs_pins_or_backend(native,policy,route):
    commands=['prepare 1','ready','change','publish 48000000','prepare 1','backend 100000']
    rows=run(native,commands,policy,1,route)
    assert rows[0]['status']==rows[4]['status']==6 and rows[1]['status']==0 and rows[-1]['status']==3
    assert all(not r['events'] and not r['board_events'] and not r['setup_held'] for r in rows)
    assert rows[-1]['resource'][2:4]==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('package,route',[(p,r) for p,r in AVAILABLE if r in CONFIG['debug_routes']])
def test_debug_route_fault_holds_domain_until_explicit_abandon(native,policy,package,route):
    commands=['change','publish 48000000','prepare 1','backend 400000','change','abandon 0','abandon 1','set32 4 67108864','prepare 1','backend 400000','close']
    rows=run(native,commands,policy,package,route)
    assert rows[2]['status']==4 and rows[2]['setup_held'] and rows[2]['board'][4]==0
    assert not any(e[0]==1 and e[1] in [0x40010004,0x40010804,0x4001101c] for e in rows[2]['board_events'])
    assert [r['status'] for r in rows[3:7]]==[2,0,0,1]
    assert rows[8]['status']==rows[9]['status']==0 and rows[-1]['resource'][2:4]==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('package,route',AVAILABLE)
def test_manager_open_rechecks_route_and_rolls_back_claim(native,policy,package,route):
    commands=[*prepared(),f'set32 4 {67108864|(((route+1)&7)<<2)}','backend 100000','change','publish 48000000','prepare 1','backend 100000','close']
    rows=run(native,commands,policy,package,route)
    assert rows[5]['status']==3 and rows[5]['resource'][2:4]==[0,0] and rows[5]['prepared']
    assert not any(e[0]==1 for e in rows[5]['events']+rows[5]['board_events'])
    assert rows[-3]['status']==rows[-2]['status']==0;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('route',[0,2])
def test_reprepare_with_old_result_retained_and_callback_during_new_owner(native,policy,route):
    commands=[*ready(),*complete(),'pump 1 0 0','prepare 1','close','change','publish 12000000','set32 1 48','prepare 1','select 1','backend 100000','open 0','bind 0 0','submit 0 1 0 1 61','dispatch','turn 2 0 2 0',*wire(0,1,[99]),'reap','close','pump 1 0 0','turn 2 0 2 0']
    rows=run(native,commands,policy,0,route);prepares=[i for i,c in enumerate(commands) if c=='prepare 1']
    assert rows[prepares[1]]['status']==2 and rows[prepares[1]]['held']==[1,0]
    assert rows[prepares[2]]['status']==0 and rows[prepares[2]]['bytes'][0]==[-1]*64
    turn=commands.index('turn 2 0 2 0')
    assert rows[turn]['callbacks']==[[1,1,0],[0,0,0]] and rows[turn]['held']==[0,1] and rows[turn]['backend'][1]
    assert rows[turn]['resource']==[12000000,2,1,7,0] and rows[turn]['bytes'][0][0]==88
    assert rows[-1]['callbacks']==[[1,1,0],[1,1,0]] and rows[-1]['bytes'][1][0]==99;collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('route',[0,2])
@pytest.mark.parametrize('which',['gate2','gate1','remap','pin'])
def test_failed_prepare_is_held_until_witness_and_explicit_retry(native,policy,route,which):
    index={'gate2':2,'gate1':3,'remap':4,'pin':5 if route<2 else 6}[which]
    commands=['change','publish 48000000',f'set32 4 {67108864|(((route+1)&7)<<2)}',f'ignore {1<<index}','prepare 1','backend 100000','change','abandon 0','ignore 0','abandon 1','prepare 1','backend 100000','close']
    rows=run(native,commands,policy,0,route)
    assert rows[4]['status']==4 and rows[4]['setup_held'] and rows[5]['status']==2
    assert rows[6]['status']==rows[7]['status']==0 and rows[9]['status']==1
    assert rows[10]['status']==rows[11]['status']==0 and rows[-1]['resource'][2:4]==[0,0];collected(rows)


@pytest.mark.parametrize('policy',['dedicated','pooled'])
@pytest.mark.parametrize('route',[0,2])
@pytest.mark.parametrize('index,value',[(0,1),(1,80),(3,0),(5,0)])
def test_stale_prepared_state_never_opens_backend(native,policy,route,index,value):
    if index==5 and route>=2:index=6
    commands=[*prepared(),f'set32 {index} {value}','backend 100000']
    rows=run(native,commands,policy,0,route)
    assert rows[-1]['status']==3 and rows[-1]['resource'][2:4]==[0,0] and rows[-1]['prepared']
    assert not any(e[0]==1 for e in rows[-1]['events']+rows[-1]['board_events']);collected(rows)
