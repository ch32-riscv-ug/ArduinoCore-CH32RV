"""Route fields, package capability and preservation of unrelated registers."""
import hashlib
import importlib.util
import inspect
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT=Path(__file__).parent;REPO=UNIT.parents[2]
INPUT=REPO/'planning/core-redesign/x035-i2c-route-inputs.json';CONFIG=json.loads(INPUT.read_text())
spec=importlib.util.spec_from_file_location('default_route_battery',UNIT/'test_x035_i2c_setup.py')
baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    root=tmp_path_factory.mktemp('x035-routes')
    source=baseline.driver_source().replace('"x035_i2c_setup.hpp"','"x035_i2c_routes.hpp"')
    old='x035_i2c_setup_experiment::Setup<BoardBus,decltype(domain)>'
    assert source.count(old)==1
    source=source.replace(old,'x035_i2c_route_experiment::Setup<BoardBus,decltype(domain),ROUTE,x035_i2c_route_experiment::Package(PACKAGE_ID)>')
    source=source.replace('"x035_setup_fake.hpp"','"x035_route_fake.inc"').replace('assert(a<6)','assert(a<7)').replace('for(unsigned i=0;i<6;++i)','for(unsigned i=0;i<7;++i)')
    old='int main(int argc,char**argv){assert(argc==2);return std::string(argv[1])=="short"?run<uint8_t>():run<uint32_t>();}'
    assert source.count(old)==1
    source=source.replace(old,'int main(int argc,char**){assert(argc==2);return run<uint32_t>();}')
    (root/'derived.cpp').write_text(source)
    fake=(UNIT/'x035_setup_fake.hpp').read_text().replace('words[6]={3,0,0,0,0,0}','words[7]={3,0,0,0,0,0,0}')
    fake=fake.replace('Remap,PinConfig};','Remap,PinConfig,0x4001101cu};').replace('i<6','i<7')
    fake=fake.replace('if(i==5)assert(words[2]&4);','if(i==5)assert(words[2]&4);\n        if(i==6)assert(words[2]&16);')
    (root/'x035_route_fake.inc').write_text(fake)
    original=(UNIT/'i2c_master_driver.cpp').read_text()
    (root/'i2c_resource_fake.inc').write_text(original[original.index('struct Fake {'):original.index('template<bool Guarded')])
    flags=['-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    out=None;sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    paths=[UNIT/n for n in ['x035_i2c_routes.hpp','x035_i2c_setup.hpp','x035_setup_fake.hpp','i2c_resources.hpp',
                           'i2c_resources_driver.cpp','i2c_master_driver.cpp','test_x035_i2c_setup.py',
                           'test_i2c_master.py',Path(__file__).name]]
    paths += [INPUT,baseline.INPUT,REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp']
    report=dict(compiler=compiler,compiler_sha256=sha(compiler),inputs={str(p.relative_to(REPO)):sha(p) for p in paths},builds=[])
    if destination:=os.environ.get('EXP01_X035_ROUTE_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        for name in ['derived.cpp','x035_route_fake.inc','i2c_resource_fake.inc']:shutil.copy2(root/name,out/name)
        for path in paths:
            dest=out/'inputs'/path.relative_to(REPO);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    binaries={}
    for package in range(2):
        for route in range(8):
            name=f'p{package}-r{route}';exe=root/name
            command=[compiler,*flags,'-I',str(root),'-I',str(UNIT),f'-DPACKAGE_ID={package}',f'-DROUTE={route}',str(root/'derived.cpp'),'-o',str(exe)]
            p=subprocess.run(command,capture_output=True,text=True,timeout=180)
            item=dict(name=name,command=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
            report['builds'].append(item)
            if out:(out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
            assert p.returncode==0,p.stderr
            item['binary_sha256']=sha(exe)
            if out:
                shutil.copy2(exe,out/name);(out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
            binaries[package,route]=exe
    return binaries


def replay(exe,commands):
    stdin='\n'.join(commands)+'\n'
    p=subprocess.run([str(exe),'normal'],input=stdin,capture_output=True,text=True,timeout=60)
    if destination:=os.environ.get('EXP01_X035_ROUTE_EVIDENCE'):
        key=hashlib.sha256((exe.name+stdin).encode()).hexdigest()
        record=dict(program=exe.name,stdin=stdin,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        path=Path(destination)/f'trace-{key}.json'
        if path.exists():assert json.loads(path.read_text())==record
        else:path.write_text(json.dumps(record,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];assert len(rows)==len(commands)
    return rows


def baseline_replay(exe,commands):
    rows=replay(exe,commands)
    for row in rows:row['board']=row['board'][:6]
    return rows


baseline.replay=baseline_replay
values=dict(hpre=range(16),hsi=range(4),
            hz=[48000000,24000000,16000000,12000000,8000000,6000000,24000000,12000000,6000000,3000000],
            index=[0,1],value=[0],ignore=[4,8,16,32],rx=[0,1,2,3,8])
# Use the existing pytest parametrize markers to preserve correlated parameter
# pairs (e.g. hpre/hz and index/value), rather than inventing their cross product.
LEGACY=[]
for name in sorted(dir(baseline)):
    if not name.startswith('test_'):continue
    function=getattr(baseline,name);parameters=list(inspect.signature(function).parameters)[1:]
    alternatives=[{}]
    for mark in getattr(function,'pytestmark',[]):
        if mark.name!='parametrize':continue
        names=[n.strip() for n in mark.args[0].split(',')]
        choices=[dict(zip(names,v if len(names)>1 else (v,))) for v in mark.args[1]]
        alternatives=[dict(a,**b) for a in alternatives for b in choices]
    for n,selection in enumerate(alternatives):
        LEGACY.append(pytest.param(function,[selection[p] for p in parameters],id=f'{name}-{n}'))
assert len(LEGACY)==100


@pytest.mark.parametrize('function,args',LEGACY)
def test_default_route_retains_full_previous_contract(native,function,args):
    function(native[0,0],*args)


AVAILABLE=[(p['id'],r) for p in CONFIG['packages'] for r in p['routes']]


@pytest.mark.parametrize('package,route',AVAILABLE)
def test_supported_routes_preserve_other_fields_and_complete_transfer(native,package,route):
    mapping=CONFIG['routes'][route]
    original_remap=0xb4f0f0ff;original_a=0xa5a5a5a5;original_c=0x5a5a5a5a
    commands=[*baseline.domain(),'set32 2 160',f'set32 4 {original_remap}',f'set32 5 {original_a}',f'set32 6 {original_c}',
              'prepare 1','open 400000','start 0 3 60','prepare 1',*baseline.protocol.address(True),
              *baseline.protocol.receive([9,8,7]),'quiet 0','step','end','take 1']
    rows=replay(native[package,route],commands)
    prepared=rows[6];assert prepared['status']==0 and prepared['domain'][2:4]==[0,0]
    assert prepared['board'][2]==160|mapping['gate_apb2']
    assert prepared['board'][4]==(original_remap&~0x1c)|(route<<2)
    index=5 if route<2 else 6;other=6 if index==5 else 5;initial=original_a if index==5 else original_c
    assert prepared['board'][index]==(initial&~mapping['mask'])|mapping['mode']
    assert prepared['board'][other]==(original_c if other==6 else original_a)
    assert rows[9]['status']==2 and not rows[9]['board_events'] and not rows[9]['events']
    assert rows[-1]['out']==[1,0,0,3,0] and rows[-1]['held']==[0,0] and rows[-1]['bytes'][:3]==[9,8,7]


@pytest.mark.parametrize('route',[0,1])
def test_f7_package_without_pa10_to_pa14_rejects_before_claim_or_mmio(native,route):
    rows=replay(native[1,route],['prepare 1','ready',*baseline.domain(),'prepare 1','open 100000'])
    assert rows[0]['status']==rows[4]['status']==6 and not rows[1]['status'] and rows[-1]['status']==3
    assert all(not r['events'] and not r['board_events'] and not r['setup_held'] for r in rows)
    assert rows[-1]['domain'][2:4]==[0,0]


@pytest.mark.parametrize('package,route',[(p,r) for p,r in AVAILABLE if r in CONFIG['debug_routes']])
def test_debug_pin_routes_require_existing_release_and_never_disable_debugger(native,package,route):
    rows=replay(native[package,route],[*baseline.domain(),'prepare 1','abandon 0','abandon 1','set32 4 67108864','prepare 1'])
    assert rows[2]['status']==4 and rows[2]['setup_held'] and rows[2]['board'][4]==0
    assert not any(e[0]==1 and e[1] in [0x40010004,0x40010804,0x4001101c] for e in rows[2]['board_events'])
    assert not rows[3]['status'] and rows[4]['status']==1 and rows[-1]['status']==0
    assert rows[-1]['board'][4]&0x07000000==0x04000000


@pytest.mark.parametrize('package,route',AVAILABLE)
def test_selected_route_configuration_is_rechecked_at_open(native,package,route):
    rows=replay(native[package,route],[*baseline.domain(),'set32 4 67108864','prepare 1',
                                     f'set32 4 {67108864|(((route+1)&7)<<2)}','open 100000'])
    assert rows[3]['status']==0 and rows[-1]['status']==3 and rows[-1]['domain'][2:4]==[0,0]
    assert not any(e[0]==1 for e in rows[-1]['events'])
