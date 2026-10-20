"""X035 register preparation with resource leases; no physical bus is run."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;REPO=UNIT.parents[2]
INPUT=REPO/'planning/core-redesign/x035-i2c-setup-inputs.json'
CONFIG=json.loads(INPUT.read_text())
spec=importlib.util.spec_from_file_location('x035_protocol',UNIT/'test_i2c_master.py')
protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)


def driver_source():
    source=(UNIT/'i2c_resources_driver.cpp').read_text()
    replacements=[
        ('#include "i2c_resources.hpp"','#include "x035_i2c_setup.hpp"'),
        ('#include "i2c_resource_fake.inc"','#include "i2c_resource_fake.inc"\n#include "x035_setup_fake.hpp"'),
        ('struct Pins { bool prepared=false;bool ready() const{return prepared;} };',''),
        ('Domain<2,Counter> domain;Pins pins;',
         'Domain<2,Counter> domain;using Pins=x035_i2c_setup_experiment::Setup<BoardBus,decltype(domain)>;Pins pins(domain);'),
        ('Port<Fake,Storage,Pins,decltype(domain)>','Port<BoardBus,Storage,Pins,decltype(domain)>'),
        ('Fake::events.clear();','Fake::events.clear();BoardBus::accesses.clear();'),
        ('else if(op=="pins"){in>>a;status=!(domain.held()&6);if(status)pins.prepared=a!=0;}',
         'else if(op=="prepare"){in>>a;status=unsigned(pins.prepare(a!=0));}\n'
         '        else if(op=="abandon"){in>>a;status=pins.abandon(a!=0);}\n'
         '        else if(op=="ready")status=pins.ready();\n'
         '        else if(op=="pclk")status=x035_i2c_setup_experiment::nominal_pclk<BoardBus>();\n'
         '        else if(op=="set32"){in>>a>>b;assert(a<6);BoardBus::words[a]=b;status=1;}\n'
         '        else if(op=="set16"){in>>a>>b;assert(a<8);Fake::regs[a]=uint16_t(b);status=1;}\n'
         '        else if(op=="ignore"){in>>a;BoardBus::ignored=a;status=1;}'),
        ('<<pins.prepared<<','<<pins.prepared()<<",\\\"setup_held\\\":"<<pins.held()<<'),
        ('std::cout<<"]}\\n";',
         'std::cout<<"],\\\"board\\\":[";for(unsigned i=0;i<6;++i)std::cout<<(i?",":"")<<BoardBus::words[i];\n'
         '        std::cout<<"],\\\"board_events\\\":[";for(unsigned i=0;i<BoardBus::accesses.size();++i){auto e=BoardBus::accesses[i];std::cout<<(i?",":"")<<\'[\'<<e.kind<<\',\'<<e.address<<\',\'<<e.value<<\']\';}\n'
         '        std::cout<<"]}\\n";'),
    ]
    for old,new in replacements:
        assert source.count(old)==1,old
        source=source.replace(old,new)
    return source


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    root=tmp_path_factory.mktemp('x035-i2c-setup');source=root/'derived.cpp';source.write_text(driver_source())
    old=(UNIT/'i2c_master_driver.cpp').read_text()
    fake=root/'i2c_resource_fake.inc';fake.write_text(old[old.index('struct Fake {'):old.index('template<bool Guarded')])
    flags=['-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    exe=root/'control';command=[compiler,*flags,'-I',str(root),'-I',str(UNIT),str(source),'-o',str(exe)]
    p=subprocess.run(command,capture_output=True,text=True,timeout=180)
    if destination:=os.environ.get('EXP01_X035_SETUP_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        paths=[UNIT/n for n in ['x035_i2c_setup.hpp','x035_setup_fake.hpp','i2c_resources.hpp',
                               'i2c_resources_driver.cpp','i2c_master_driver.cpp','test_i2c_master.py',Path(__file__).name]]
        paths += [INPUT,REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp']
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        for path in paths:
            dest=out/'inputs'/path.relative_to(REPO);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
        shutil.copy2(source,out/source.name);shutil.copy2(fake,out/fake.name)
        report=dict(compiler=compiler,compiler_sha256=sha(compiler),command=command,returncode=p.returncode,
                    stdout=p.stdout,stderr=p.stderr,inputs={str(p.relative_to(REPO)):sha(p) for p in paths},
                    derived_sha256=sha(source),fake_sha256=sha(fake))
        if p.returncode==0:shutil.copy2(exe,out/'native-control');report['binary_sha256']=sha(exe)
        (out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    return exe


def replay(native,commands):
    stdin='\n'.join(commands)+'\n'
    p=subprocess.run([str(native),'normal'],input=stdin,capture_output=True,text=True,timeout=60)
    if destination:=os.environ.get('EXP01_X035_SETUP_EVIDENCE'):
        key=hashlib.sha256(stdin.encode()).hexdigest()
        record=dict(stdin=stdin,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        path=Path(destination)/f'trace-{key}.json'
        if path.exists():assert json.loads(path.read_text())==record
        else:path.write_text(json.dumps(record,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];assert len(rows)==len(commands)
    for row in rows:
        if row['setup_held']:assert row['domain'][2:4]==[1,7] and not any(row['held'])
    return rows


def domain(hz=48000000):return ['change',f'publish {hz}']


@pytest.mark.parametrize('hpre',range(16))
@pytest.mark.parametrize('hsi',range(4))
def test_all_clock_dividers_and_hsi_confirmation(native,hpre,hsi):
    rows=replay(native,[f'set32 0 {hsi}',f'set32 1 {hpre<<4}','pclk'])
    divisor=CONFIG['hpre_divisors'][hpre]
    assert rows[-1]['status']==(48000000//divisor if hsi==3 else 0)


@pytest.mark.parametrize('hpre,hz',[(0,48000000),(1,24000000),(2,16000000),(3,12000000),(5,8000000),
                                    (7,6000000),(8,24000000),(9,12000000),(10,6000000),(11,3000000)])
def test_register_preparation_preserves_other_fields_and_supplies_port_ready(native,hpre,hz):
    commands=[*domain(hz),f'set32 1 {hpre<<4}','set32 2 160','set32 3 64',
              'set32 4 4042322175','set32 5 2779096485','prepare 1','ready','open 100000','end']
    rows=replay(native,commands)
    assert rows[-4]['status']==0 and rows[-4]['prepared'] and not rows[-4]['setup_held']
    assert rows[-4]['board'][2:]==[165,64|0x200000,4042322175&~0x1c,(2779096485&~0xff00)|0x9900]
    assert rows[-3]['status']==1 and rows[-2]['status']==0 and rows[-1]['domain'][2:4]==[0,0]


def test_cold_proof_domain_and_clock_mismatch_reject_before_writes(native):
    rows=replay(native,['prepare 0','prepare 1',*domain(12000000),'prepare 1','set32 1 48','prepare 1'])
    assert rows[0]['status']==5 and rows[1]['status']==2 and rows[4]['status']==3
    for row in rows[:5]:assert not any(e[0] for e in row['board_events'])
    assert rows[4]['domain'][2:4]==[0,0] and rows[-1]['status']==0


@pytest.mark.parametrize('index,value',[(0,1),(0,0x100),(0,0x200),(0,0x8000),
                                     (1,0x100),(1,0x200),(1,0x400),(1,0x800),(1,0x1000),
                                     (6,1),(6,2),(6,3)])
def test_unproven_controller_idle_holds_lease_and_does_not_change_route_or_pins(native,index,value):
    commands=[*domain(),f'set16 {index} {value}','prepare 1','change','open 100000','abandon 0',
              f'set16 {index} 0','abandon 1','prepare 1']
    rows=replay(native,commands)
    assert rows[3]['status']==4 and rows[3]['setup_held'] and rows[3]['board'][4:]==[0,0]
    assert not rows[4]['status'] and rows[5]['status']==2 and not rows[6]['status']
    assert rows[-2]['status']==1 and rows[-1]['status']==0
    assert not any(e[0]==1 and e[1] in [0x40010004,0x40010804] for e in rows[3]['board_events'])


@pytest.mark.parametrize('ignore',[1<<2,1<<3,1<<4,1<<5])
def test_ignored_write_detected_without_accessing_clock_gated_registers(native,ignore):
    rows=replay(native,[*domain(),'set32 4 28','ignore '+str(ignore),'prepare 1','abandon 0',
                        'ignore 0','abandon 1','prepare 1'])
    assert rows[4]['status']==4 and rows[4]['setup_held'] and not rows[5]['status']
    assert rows[-1]['status']==0


@pytest.mark.parametrize('index,value',[(1,80),(3,0),(4,4),(5,0)])
def test_open_rechecks_real_registers_instead_of_trusting_old_prepared_flag(native,index,value):
    rows=replay(native,[*domain(),'prepare 1',f'set32 {index} {value}','ready','open 100000'])
    assert rows[2]['status']==0 and rows[4]['status']==0 and rows[-1]['status']==3
    assert rows[-1]['domain'][2:4]==[0,0] and rows[-1]['prepared']
    assert not any(e[0]==1 for e in rows[-1]['events'])


@pytest.mark.parametrize('rx',[0,1,2,3,8])
def test_prepare_cannot_interfere_with_borrowed_transfer_and_reopen_rechecks_configuration(native,rx):
    commands=[*domain(),'prepare 1','open 400000',f'start {0 if rx else 1} {rx} 60','prepare 1',*protocol.address(bool(rx))]
    commands+=protocol.receive(list(range(10,10+rx))) if rx else protocol.transmit(1)
    commands+=['quiet 0','step','ack 1','end','prepare 1','take 1','prepare 1','open 100000','end']
    rows=replay(native,commands)
    assert rows[5]['status']==2 and not rows[5]['board_events'] and not rows[5]['events']
    assert rows[-5]['status']==2 and rows[-4]['held']==[0,0]
    assert rows[-3]['status']==0 and rows[-2]['status']==0 and rows[-1]['domain'][2:4]==[0,0]
