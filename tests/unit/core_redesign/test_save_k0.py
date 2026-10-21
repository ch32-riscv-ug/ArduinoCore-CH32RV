"""N05 V003 K0 + bounded scan + synchronous 64-byte media operation.

The fixed N03 register fixture is conditional, not a physical I2C emulator.
"""
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
spec=importlib.util.spec_from_file_location('save_k0_reference',HERE/'test_k0_slave_register.py')
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    directory=tmp_path_factory.mktemp('save-k0')
    source=(HERE/'k0_slave_register_driver.cpp').read_text()
    generated={'save_k0_registers.inc':source[source.index('struct Gate {'):source.index('template<unsigned C,unsigned N,unsigned P>')]}
    source=(HERE/'save_x0_driver.cpp').read_text()
    medium=source[source.index('struct Memory {'):source.index('template<unsigned Units> struct Geometry')]
    # Fault-mask sentinel 256 remains unchanged; only physical geometry changes.
    for before,after in [('size==256','size==64'),('offset%256','offset%64'),('offset/256','offset/64')]:
        assert before in medium;medium=medium.replace(before,after)
    generated['save_k0_memory.inc']=medium
    for name,body in generated.items():(directory/name).write_text(body)
    command=[compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I',str(directory),str(HERE/'save_k0_driver.cpp'),'-o',str(directory/'app')]
    r=subprocess.run(command,capture_output=True,text=True,timeout=300)
    if destination:=os.environ.get('EXP06_SAVE_K0_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        paths=[Path(__file__),REPO/'planning/core-redesign/save-k0-inputs.json',*sorted(HERE.glob('*.hpp'))]
        paths += [HERE/n for n in ['save_k0_driver.cpp','save_x0_driver.cpp','k0_slave_register_driver.cpp','test_k0_slave_register.py','test_k0_candidates.py','oracle.py']]
        for p in paths:
            target=out/'inputs'/p.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        for name in generated:shutil.copy2(directory/name,out/name)
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        report=dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,
                    compiler_version=subprocess.check_output([compiler,'--version'],text=True),compiler_sha256=sha(compiler),
                    generated={n:sha(directory/n) for n in generated},inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if not r.returncode:shutil.copy2(directory/'app',out/'app');report['executable_sha256']=sha(directory/'app')
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert r.returncode==0,r.stderr
    return directory/'app'


def run(native,commands,candidate=1,units=2,policy=0,coalesced=False,capacity=4):
    r=subprocess.run([str(native),str(candidate),str(units),str(policy),str(int(coalesced)),str(capacity)],
                     input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    for cmd,row in zip(commands,rows):
        if cmd in ['storage','turn']:assert row['reads']<=32 and len(row['events'])<=1
        elif not cmd.startswith('run '):assert not row['reads'] and not row['events']
        if row['events']:
            assert row['save'][3:5]==[1,1]
            assert all(e[2]==64 for e in row['events'])
        if cmd=='storage':assert not row['mmio']
        assert row['clock'][1]<=row['clock'][0]//1000
    if destination:=os.environ.get('EXP06_SAVE_K0_EVIDENCE'):
        trace=dict(candidate=candidate,units=units,policy=policy,coalesced=coalesced,capacity=capacity,commands=commands,rows=rows)
        key=hashlib.sha256(json.dumps(trace,sort_keys=True).encode()).hexdigest()
        Path(destination,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(trace,separators=(',',':')).encode(),mtime=0))
    return rows


def scan():return ['storage']*20
def steps(units):return 3 if units==4 else 2
def update(tag,units):return [f'save {tag} 0']+['storage']*steps(units)+['take']+scan()


def decode(data,units):
    size=len(data)//(2 if units>1 else 1);valid=[];dirty=False
    for bank in range(2 if units>1 else 1):
        block=bytes(data[bank*size:(bank+1)*size])
        if block==b'\xff'*size:continue
        dirty=True
        magic,generation,length,version,crc=struct.unpack('<4sIHHI',block[:16])
        if magic==b'BLB1' and generation and length==8 and version==1 and zlib.crc32(block[:12]+block[16:24])==crc:
            value,tag=struct.unpack('<II',block[16:24]);valid.append((generation,bank,value,tag))
    if valid:
        generation,bank,value,tag=max(valid);return [1,1,generation,bank,value,tag]
    return [1,2 if dirty else 0,0,255,0,0]


@pytest.mark.parametrize('candidate',range(1,5))
@pytest.mark.parametrize('units',[0,1,2,4])
def test_capture_negative_position_and_result_hold(native,candidate,units):
    commands=['save 1 0']+scan()+['input -7','app','save 9 0','input 2','app']+['storage']*steps(units)+['save 10 0','observe','take']+scan()+['dump']
    rows=run(native,commands,candidate,units)
    assert rows[-1]['meaning'][0]==-5
    if not units:
        assert rows[0]['status']==1 and all(not r['reads'] and not r['events'] for r in rows);return
    assert rows[0]['status']==2
    i=commands.index('save 10 0');assert rows[i]['status']==3 and rows[i]['save'][5]==1
    assert rows[i+1]['clock']==rows[i]['clock'] and rows[i+2]['result_out']==1
    assert rows[-1]['snapshot']==decode(rows[-1]['memory'],units)
    assert rows[-1]['snapshot'][4:]==[2**32-7,9]


@pytest.mark.parametrize('units',[0,1,2,4])
@pytest.mark.parametrize('coalesced',[False,True])
@pytest.mark.parametrize('capacity',[1,4])
def test_measured_sample_timing_as_declared_input(native,units,coalesced,capacity):
    commands=scan()+['rate 1000 1','save 7 0']+['storage']*steps(units)+['observe','deliver','app']
    all_rows=[run(native,commands,c,units,0,coalesced,capacity) for c in range(1,5)]
    assert all(r==all_rows[0] for r in all_rows)
    row=all_rows[0][-1]
    if not units:assert row['clock'][:3]==[0,0,0];return
    erases=2 if units==4 else 1
    elapsed=erases*3130+2810+86
    assert row['clock'][0]==elapsed and row['clock'][2]==erases+1
    assert row['input']==[elapsed//1000,min(capacity,elapsed//1000),max(0,elapsed//1000-capacity),0]
    assert row['meaning'][0]==min(capacity,elapsed//1000)
    assert row['clock'][1]==(erases+1 if coalesced else elapsed//1000)


@pytest.mark.parametrize('policy',[0,1])
@pytest.mark.parametrize('point',['addr','rx2','overrun','tx'])
def test_pending_slave_state_is_only_serviced_explicitly(native,policy,point):
    initial=scan()+['input 7','app','save 3 0']
    if point=='addr':initial+=['write']
    elif point in ['rx2','overrun']:
        initial+=['write','irq','byte 1','byte 2']+(['byte 3'] if point=='overrun' else [])
    else:initial+=['read','irq','irq']
    commands=initial+['storage','storage','observe','irq']
    rows=[run(native,commands,c,2,policy) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    before=rows[0][len(initial)-1]
    for row in rows[0][len(initial):len(initial)+3]:
        for key in ['meaning','frontend','port','published']:assert row[key]==before[key]
    final=rows[0][-1]
    if point=='addr':assert final['port'][8]==before['port'][8]+1 and final['frontend'][0]==1
    elif point=='rx2':assert final['port'][6]==0 and final['frontend'][2]==2
    elif point=='overrun':assert final['port'][0]==4 and final['port'][1]&1025==0
    else:assert final['port'][7]==2


@pytest.mark.parametrize('policy',[0,1])
def test_valid_frame_and_read_snapshot_across_storage(native,policy):
    frame=reference.frame(23,2,pair=True)
    commands=scan()+frame+['input 3','app','save 8 0','read','irq','irq','storage','input 1','app','storage']
    commands+=['consume 0','irq']*7+['consume 1','irq','stop','irq','take']+scan()+['dump']
    rows=[run(native,commands,c,2,policy) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    replies=[r['port'][5] for cmd,r in zip(commands,rows[0]) if cmd.startswith('consume')]
    assert replies==[6,0,0,0,23,0,0,0] and rows[0][-1]['meaning'][:5]==[8,2,-1000,1000,23]
    assert rows[0][-1]['snapshot']==decode(rows[0][-1]['memory'],2)
    assert rows[0][-1]['snapshot'][4:]==[6,8]


@pytest.mark.parametrize('units',[1,2,4])
@pytest.mark.parametrize('fault',[0,255,256,257,258])
def test_replacement_erase_failure_one_unit_can_lose_old(native,units,fault):
    commands=scan()+['input 5','app']+update(1,units)+['input 2','app','save 2 0',f'cut {fault}','storage','take']+scan()+['dump','reboot']+scan()+['dump']
    rows=[run(native,commands,c,units) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    for row in [rows[0][commands.index('dump')],rows[0][-1]]:
        # Unstable first byte is an explicit narrow deterministic fault model.
        if fault!=258:assert row['snapshot']==decode(row['memory'],units)
        if units>1:assert row['snapshot'][4:]==[5,1]
        elif fault==0:assert row['snapshot'][4:]==[5,1] # Failed op can leave old bytes intact.
        elif fault in [255,256]:assert row['snapshot'][1]==0 # Old record erased; rest of this small page was already FF.
        else:assert row['snapshot'][1]==2
    assert rows[0][-1]['meaning'][0]==0 # No implicit app-state restore on boot.


@pytest.mark.parametrize('units',[1,2,4])
@pytest.mark.parametrize('fault',[0,255,256])
def test_program_failure_can_publish_new_despite_false_return(native,units,fault):
    commands=scan()+['input 5','app']+update(1,units)+['input 2','app','save 2 0']
    commands+=['storage']*(2 if units==4 else 1)+[f'cut {fault}','storage','take']+scan()+['dump']
    rows=[run(native,commands,c,units) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    row=rows[0][-1];assert row['result_out']==0 and row['snapshot']==decode(row['memory'],units)
    if fault in [255,256]:assert row['snapshot'][4:]==[7,2] # Half page already includes header+blob8.
    elif units>1:assert row['snapshot'][4:]==[5,1]
    else:assert row['snapshot'][1]==0 # Erased only bank, failed write made no changes.


@pytest.mark.parametrize('seed',range(10))
def test_random_lifecycle_all_candidates(native,seed):
    randomizer=random.Random(seed);commands=scan()
    for _ in range(120):
        choice=randomizer.randrange(7)
        if choice==0:commands+=['reboot']+scan()
        elif choice==1:commands+=['take']+scan()
        elif choice==2:commands+=[f'save {randomizer.randrange(100)} 1']
        elif choice==3:commands+=['rearm']+reference.frame(randomizer.randrange(100),randomizer.randrange(1,5),pair=True)
        elif choice==4:commands+=[f'input {randomizer.randrange(-3,4)}','app']
        elif choice==5:commands+=['storage']
        else:commands+=['observe']
    commands+=['dump']
    rows=[run(native,commands,c,1 if seed%2 else 2,seed%2) for c in range(1,5)]
    assert all(r==rows[0] for r in rows)
    assert rows[0][-1]['snapshot']==decode(rows[0][-1]['memory'],1 if seed%2 else 2) or not rows[0][-1]['snapshot'][0]


@pytest.fixture(scope='module')
def slave_native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    output=tmp_path_factory.mktemp('save-k0-old')/'app'
    command=[compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(HERE/'k0_slave_register_driver.cpp'),'-o',str(output)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=300)
    assert result.returncode==0,result.stderr
    if destination:=os.environ.get('EXP06_SAVE_K0_EVIDENCE'):
        out=Path(destination);shutil.copy2(output,out/'reference-app')
        (out/'reference-build.json').write_text(json.dumps(dict(command=command,returncode=result.returncode,
            stderr=result.stderr,stdout=result.stdout,executable_sha256=hashlib.sha256(output.read_bytes()).hexdigest()),indent=2)+'\n')
    return output


def test_disabled_storage_preserves_frozen_slave_fixture(native,slave_native):
    commands=reference.frame(23,2,pair=True)+['input 3','step']+reference.read(8)+reference.frame(24,3)+['step']
    for candidate in range(1,5):
        for policy in [0,1]:
            old=reference.run(slave_native,commands,candidate,policy=policy)
            new=run(native,['app' if c=='step' else c for c in commands],candidate,0,policy)
            for before,after in zip(old,new):
                for key in ['meaning','frontend','published']:assert before[key]==after[key]
                assert before['port'][:8]+before['port'][9:]==after['port']
                assert len(before['log'])==after['mmio']


@pytest.mark.parametrize('units',[1,2,4])
@pytest.mark.parametrize('boundary',range(4))
def test_cold_model_boot_each_update_boundary(native,units,boundary):
    commands=scan()+['input 5','app']+update(1,units)+['input 2','app','save 2 0']+['storage']*boundary
    commands+=['reboot']+scan()+['dump']
    rows=[run(native,commands,c,units) for c in range(1,5)];assert all(r==rows[0] for r in rows)
    row=rows[0][-1];assert row['snapshot']==decode(row['memory'],units)
    if boundary==0 or (units>1 and boundary<steps(units)):assert row['snapshot'][4:]==[5,1]
    elif boundary>=steps(units):assert row['snapshot'][4:]==[7,2]
    else:assert row['snapshot'][1]==0
    assert row['meaning'][0]==0 and row['result_out']==123


@pytest.mark.parametrize('coalesced',[False,True])
@pytest.mark.parametrize('capacity',[1,4])
def test_explicit_normal_loop_with_storage(native,coalesced,capacity):
    commands=['rate 1000 1','run 50000 1000','save 7 0','run 100000 1000','observe']
    last=[]
    for units in [0,1,2,4]:
        rows=[run(native,commands,c,units,0,coalesced,capacity) for c in range(1,5)]
        assert all(r==rows[0] for r in rows);last.append(rows[0][-1])
    assert last[0]['input']==[100,100,0,0]
    if capacity==1:assert all(r['input'][2]>0 for r in last[1:])
    else:assert all(r['input'][2]==0 for r in last[1:])
    assert all(r['meaning'][0]==r['input'][1] for r in last)
