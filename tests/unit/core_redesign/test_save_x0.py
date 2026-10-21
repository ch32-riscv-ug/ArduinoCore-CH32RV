"""N05 synchronous storage blackout inside full prepared X0 register application."""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import zlib

import pytest

HERE=Path(__file__).parent
REPO=HERE.parents[2]
spec=importlib.util.spec_from_file_location('save_x0_reference',HERE/'test_x0_register_wire.py')
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('save-x0');source=(HERE/'i2c_master_driver.cpp').read_text()
    generated={'i2c_handoff_fake.inc':source[source.index('struct Fake {'):source.index('template<bool Guarded')]}
    board=(HERE/'x035_setup_fake.hpp').read_text().replace('words[6]={3,0,0,0,0,0}','words[7]={3,0,0,0,0,0,0}')
    board=board.replace('Remap,PinConfig};','Remap,PinConfig,0x4001101cu};').replace('i<6','i<7').replace('if(i==5)assert(words[2]&4);','if(i==5)assert(words[2]&4);\n        if(i==6)assert(words[2]&16);')
    generated['x035_route_fake.inc']=board
    source=(HERE/'x0_prepared_wire_driver.cpp').read_text();generated['x0_prepared_helpers.inc']=source[source.index('using Resources ='):source.index('template<unsigned Candidate,bool WithEvents')]
    for name,text in generated.items():(directory/name).write_text(text)
    command=[compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(directory),str(HERE/'save_x0_driver.cpp'),'-o',str(directory/'app')]
    r=subprocess.run(command,capture_output=True,text=True,timeout=300)
    if destination:=os.environ.get('EXP06_SAVE_X0_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        # Preserve all repository-local transitive prototype inputs, including
        # runtime i2c header; production core/USB sources are not collected.
        paths=[Path(__file__),REPO/'planning/core-redesign/save-x0-inputs.json',REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp']
        paths += sorted(HERE.glob('*.hpp'))
        paths += [HERE/n for n in ['save_x0_driver.cpp','i2c_master_driver.cpp','x0_prepared_wire_driver.cpp','test_x0_register_wire.py','x0_wire_oracle.py','app_oracle.py','oracle.py']]
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        for name in generated:shutil.copy2(directory/name,out/name)
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        report=dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,compiler_sha256=sha(compiler),
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),generated={k:sha(directory/k) for k in generated},
                    inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if not r.returncode:shutil.copy2(directory/'app',out/'app');report['executable_sha256']=sha(directory/'app')
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==0,r.stderr
    return directory/'app'


def run(native,commands,candidate=1,units=2,coalesced=False,capacity=4):
    r=subprocess.run([str(native),str(candidate),str(units),str(int(coalesced)),str(capacity)],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    for command,row in zip(commands,rows):
        if command in ['storage','turn']:assert row['reads']<=32 and len(row['events'])<=1
        elif not command.startswith('run '):assert row['reads']==0 and row['events']==[]
        if row['events']:
            assert row['save'][3:5]==[1,1]
            if command=='storage':assert row['mmio']==0
        assert row['clock'][1]<=row['clock'][0]//1000
    if destination:=os.environ.get('EXP06_SAVE_X0_EVIDENCE'):
        d=dict(candidate=candidate,units=units,coalesced=coalesced,capacity=capacity,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(d,sort_keys=True).encode()).hexdigest()
        Path(destination,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(d,separators=(',',':')).encode(),mtime=0))
    return rows


def scan():return ['storage']*40
def steps(units):return 3 if units==4 else 2
def update(tag,units):return [f'save {tag} 0']+['storage']*steps(units)+['take']+scan()


def decode(data,units):
    bank_bytes=len(data)//(2 if units>1 else 1);valid=[];dirty=False
    for bank in range(2 if units>1 else 1):
        block=bytes(data[bank*bank_bytes:(bank+1)*bank_bytes])
        if block==b'\xff'*len(block):continue
        dirty=True
        magic,generation,size,version,crc=struct.unpack('<4sIHHI',block[:16])
        if magic==b'BLB1' and generation and size==8 and version==1 and zlib.crc32(block[:12]+block[16:24])==crc:
            value,tag=struct.unpack('<II',block[16:24]);valid.append((generation,bank,value,tag))
    if valid:
        generation,bank,value,tag=max(valid)
        return [1,1,generation,bank,value,tag]
    return [1,2 if dirty else 0,0,255,0,0]


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('units',[0,1,2,4])
def test_mount_capture_result_hold_and_explicit_rescan(native,candidate,units):
    commands=['save 1 0']+scan()+['key 1','app','save 9 0','key 1','app']+['storage']*steps(units)+['save 10 0','observe','take']+scan()+['dump']
    rows=run(native,commands,candidate,units)
    if not units:
        assert rows[0]['status']==1 and rows[-1]['save']==[99,99,99,0,0,0,0,0,0]
        assert all(r['reads']==0 and not r['events'] for r in rows)
        return
    assert rows[0]['status']==2 # mount scan not done, no implicit acceptance scan.
    index=commands.index('save 10 0');assert rows[index]['status']==3
    assert rows[index]['save'][0]==4 and rows[index]['save'][5]==1
    assert rows[index+1]['clock']==rows[index]['clock']
    assert rows[index+2]['result_out']==1 and rows[index+2]['save'][0]==1
    assert rows[-1]['snapshot']==decode(rows[-1]['memory'],units)
    assert rows[-1]['snapshot'][4:]==[5020,9] and rows[-1]['pps'][0]==5040


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('coalesced',[False,True])
@pytest.mark.parametrize('capacity',[1,4])
def test_blackout_delays_normal_work_and_has_separate_input_capacity(native,candidate,coalesced,capacity):
    commands=scan()+['attach','key 0','app','rate 1000 1','latency 100000 100000 0','save 7 0','storage','storage','observe','deliver','app']
    rows=run(native,commands,candidate,2,coalesced,capacity)
    index=commands.index('save 7 0');before=rows[index]
    erased,programmed,still=rows[index+1:index+4]
    assert erased['clock'][:2]==[100000,1 if coalesced else 100]
    assert programmed['clock'][:2]==[200000,2 if coalesced else 200]
    assert programmed['pps'][7:11]==[1,0,1,1] # physical completion latched, not collected.
    assert programmed['periodic']==before['periodic']
    assert programmed['wire']==before['wire'] and programmed['mmio']==0
    assert still['clock']==programmed['clock'] and still['input']==programmed['input']
    assert programmed['input']==[200,0,200-capacity,capacity]
    assert rows[-1]['pps'][0]==5000+capacity*20
    assert rows[-1]['pps'][1:3]==[5000,1]
    assert rows[-1]['periodic'][2:4]==([0,0] if coalesced else [2,10])


@pytest.mark.parametrize('candidate',range(1,5))
def test_completed_i2c_loan_is_not_reclaimed_by_storage(native,candidate):
    commands=scan()+['app','finish','observe','save 3 0','storage','storage','app']
    rows=run(native,commands,candidate)
    first=rows[commands.index('observe')]
    assert first['wire'][0:2]==[1,1] and first['wire'][-1]==1
    for index in [commands.index('storage',40),commands.index('storage',40)+1]:
        assert rows[index]['wire']==first['wire']
    assert rows[-1]['wire'][5]==first['wire'][5]+1


FAULTS=[(units,position,mask) for units in [1,2,4] for position in range(steps(units))
        for mask in ([0,255,256] if position==steps(units)-1 else [0,255,256,257,258])]


@pytest.mark.parametrize('units,position,mask',FAULTS)
def test_quiescent_failure_rescan_and_explicit_recovery(native,units,position,mask):
    commands=scan()+update(1,units)+['key 1','app','save 2 0']+['storage']*position+[f'cut {mask}','storage','take']+scan()+['dump']
    for candidate in range(1,5):
        rows=run(native,commands,candidate,units)
        failure=commands.index(f'cut {mask}')+1
        assert rows[failure]['save'][0]==5 and rows[failure]['save'][5:7]==[1,0]
        assert rows[failure+1]['result_out']==0
        assert rows[-1]['snapshot']==decode(rows[-1]['memory'],units)
        if units>1:assert rows[-1]['snapshot'][1]==1
        recovered=run(native,commands+['save 3 1']+['storage']*steps(units)+['take']+scan()+['dump'],candidate,units)
        assert recovered[-1]['snapshot']==decode(recovered[-1]['memory'],units)
        assert recovered[-1]['snapshot'][4:]==[5020,3]


@pytest.mark.parametrize('units',[1,2,4])
@pytest.mark.parametrize('position',range(4))
def test_cold_model_boot_rebuilds_cache_without_auto_app_restore(native,units,position):
    commands=scan()+['key 1','app']+update(8,units)+['save 9 0']+['storage']*min(position,steps(units))+['reboot']+scan()+['dump']
    rows=run(native,commands,units=units)
    assert rows[-1]['snapshot']==decode(rows[-1]['memory'],units)
    assert rows[-1]['pps'][0]==5000 # Persisted settings are exposed, not silently applied.
    if units>1 or position==0 or position>=steps(units):assert rows[-1]['snapshot'][1]==1


def test_no_storage_keeps_frozen_prepared_application_meaning(native):
    commands=['attach','key 1','key 0','app','wait 6000','app','finish','app','wait 25000','finish','app','finish','app']
    model=reference.Model();wanted=[];wall=0
    for command in commands:
        if command.startswith('wait '):wall+=int(command.split()[1]);model.apply(f'time {wall//1000}')
        elif command=='app':model.apply('step')
        elif command=='finish':model.apply('wire')
        else:model.apply(command)
        wanted.append(model.observe())
    for candidate in range(1,5):
        rows=run(native,commands,candidate,0)
        for row,w in zip(rows,wanted):
            assert row['pps'][:4]==[w['desired'],w['confirmed'],w['valid'],w['stale']]
            assert row['wire'][:6]==[w[k] for k in ['active','terminal','owner','started','completed','reclaimed']]
            assert row['periodic']==[w[k] for k in ['measure_done','touch_done','missed_measure','missed_touch','frames']]


@pytest.mark.parametrize('coalesced',[False,True])
@pytest.mark.parametrize('capacity',[1,4])
def test_normal_loop_sensitivity_with_and_without_saving(native,coalesced,capacity):
    baseline=['attach','rate 1000 1','run 50000 1000','save 7 0','run 100000 1000','observe']
    results=[]
    for units in [0,1,2,4]:
        rows=[run(native,baseline,c,units,coalesced,capacity) for c in range(1,5)]
        assert all(r==rows[0] for r in rows[1:])
        results.append(rows[0][-1])
    assert results[0]['input'][2]==0
    assert all(r['wire'][5]>0 and r['pps'][0]>5000 for r in results)
    if capacity==1:assert all(r['input'][2]>0 for r in results[1:])
    else:assert all(r['input'][2]==0 for r in results)


@pytest.mark.parametrize('seed',range(10))
def test_random_full_application_storage_lifecycle(native,seed):
    rng=random.Random(seed);commands=['attach','rate 1000 1'];end=0
    for cycle in range(100):
        if rng.randrange(20)==0:
            commands+=['reboot','attach','rate 1000 1'];end=0
        end+=50000
        commands += [f'run {end} 1000',f'save {cycle+1} 1',rng.choice(['take','app','finish','observe']),
                     f'wait {rng.randrange(3000)}',rng.choice(['turn','storage','take','finish'])]
    units=[0,1,2,4][seed%4];clock=bool(seed&1);capacity=1 if seed%3 else 4
    rows=[run(native,commands,c,units,clock,capacity) for c in range(1,5)]
    assert all(r==rows[0] for r in rows[1:])
    assert rows[0][-1]['wire'][5]>0
