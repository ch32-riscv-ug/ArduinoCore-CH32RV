"""Same on-media format, leased bounded preparation, and no acceptance-time read."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

ROOT=Path(__file__).resolve().parents[3];UNIT=Path(__file__).parent
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
media=module('integrated_media',UNIT/'flash_model.py')
formats=module('integrated_formats',UNIT/'flash_formats.py')
config=json.loads((ROOT/'planning/core-redesign/flash-format-inputs.json').read_text())
geometry=json.loads((ROOT/'planning/core-redesign/flash-inputs.json').read_text())
CASES=[(t['part'],mode,g['program_unit'],g['erase_unit'],u,i,c) for t in geometry['targets'] for mode,g in t['modes'].items() for u in config['erase_units'] for i,c in enumerate(config['candidates'])]
BASE=0x08000000
def make(case):
    c=case[6];return formats.Store(media.Medium(case[2],case[3],case[4]),kind=c['kind'],policy=c['policy'],commit=c['commit'])
def case_id(case):return f'{case[0]}:{case[1]}:{case[4]}:{case[6]["id"]}'
SUPPORTED=[case for case in CASES if make(case).layout()['supported']]
UNSUPPORTED=[case for case in CASES if not make(case).layout()['supported']]


@pytest.fixture(scope='module')
def integrated(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    executable=tmp_path_factory.mktemp('flash-integrated')/'control'
    flags=['-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    command=[compiler,*flags,str(UNIT/'flash_integrated_driver.cpp'),'-o',str(executable)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=180);assert result.returncode==0,result.stderr
    evidence=os.environ.get('EXP04_INTEGRATED_EVIDENCE')
    if evidence:
        evidence=Path(evidence);evidence.mkdir(parents=True,exist_ok=False);shutil.copy2(executable,evidence/'control')
        inputs={}
        for path in [UNIT/'flash_integrated_driver.cpp',UNIT/'flash_integrated.hpp',UNIT/'flash_scan.hpp',UNIT/'flash_store.hpp',UNIT/'flash_model.py',UNIT/'flash_formats.py',Path(__file__),ROOT/'planning/core-redesign/flash-inputs.json',ROOT/'planning/core-redesign/flash-format-inputs.json']:
            relative=path.relative_to(ROOT);dest=evidence/'inputs'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest);inputs[str(relative)]=hashlib.sha256(path.read_bytes()).hexdigest()
        version=subprocess.run([compiler,'--version'],capture_output=True,text=True,check=True).stdout
        (evidence/'build.json').write_text(json.dumps(dict(command=command,compiler_version=version,compiler_sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),inputs=inputs,stdout=result.stdout,stderr=result.stderr,returncode=result.returncode),indent=2)+'\n')
    return executable,evidence


def encode(value):return b''.join(struct.pack('<HI',k,value[k]) for k in sorted(value)) if isinstance(value,dict) else value
def begin(value,recover=False):
    data=encode(value);return f'begin {len(data)} {int(recover)} '+' '.join(map(str,data))
def value(case,n):return {7:119*n,11:991*n} if case[6]['kind']=='keys' else [b'',b'first-v1',b'next-v2!',b'last-v3!'][n]
def snapshot(store):
    s=store.load();return dict(load=['empty','valid','corrupt','ambiguous','unvalidated','no-space'].index(s['status']),generation=s['generation'],bank=255 if s['bank'] is None else s['bank'],payload=[] if s['value'] is None else list(encode(s['value'])))


class Script:
    def __init__(self,case):self.case=case;self.commands=[];self.expected=[];self.store=make(case)
    def add(self,command,**want):self.commands.append(command);self.expected.append(want)
    def scan(self,command='rescan',budget=32):
        self.add(command,status=0 if command=='open' else 1,owned=1,phase=1)
        self.add('poll 0',phase=1,reads=[])
        count=(self.case[3]*self.case[4]+128+min(budget,32)-1)//min(budget,32)
        for _ in range(count):self.add(f'poll {budget}')
        self.add('peek',status=1,phase=2,ready=1,**snapshot(self.store))
    def operation(self,cut=None):
        s=self.store;kind,offset,data=s.operations[s.position];n=s.medium.erase_unit if kind=='erase' else s.medium.program_unit
        mask=None if cut is None else (bytes([255])*(n//2)+bytes(n-n//2) if cut==256 else bytes([cut])*n)
        if cut is not None:self.add(f'cut {cut}')
        try:phase=4 if s.step(mask)=='done' else 3
        except media.PowerCut:phase=5
        event=[0 if kind=='erase' else 1,offset,n,int(cut is None),list(data)]
        self.add('step',phase=phase,status=phase,events=[event],ready=0,reads=[])
    def update(self,data,recover=False):
        self.store.begin(data,recover_corrupt=recover);self.add(begin(data,recover),status=0,phase=3,ready=0,reads=[])
        self.add('poll 0',phase=3,events=[],reads=[])
        self.add('peek',status=0,ready=0)
        while self.store.active is not None:self.operation()
        self.add(begin(data),status=1,phase=4,reads=[])
        self.add('peek',status=0,ready=0)
        self.scan()
    def check(self,native):
        executable,evidence=native;args=[self.case[5],self.case[2],self.case[3],self.case[4]];stdin='\n'.join(self.commands)+'\n'
        result=subprocess.run([str(executable),*map(str,args)],input=stdin,capture_output=True,text=True,timeout=90)
        if evidence:
            key=hashlib.sha256((json.dumps(args)+'\n'+stdin).encode()).hexdigest()
            (evidence/(key+'.json')).write_text(json.dumps(dict(args=args,stdin=stdin,stdout=result.stdout,stderr=result.stderr,returncode=result.returncode),indent=2)+'\n')
        assert result.returncode==0,result.stderr;assert not result.stderr
        rows=[json.loads(line) for line in result.stdout.splitlines()];assert len(rows)==len(self.commands)
        for command,row,want in zip(self.commands,rows,self.expected):
            for k,v in want.items():assert row[k]==v,(case_id(self.case),command,k,row[k],v)
            if command.startswith('poll '):
                assert len(row['reads'])<=min(int(command.split()[1]),32)
            else:assert row['reads']==[],(command,row)
            if command!='step':assert row['events']==[],(command,row)
            assert len(row['events'])<=1
            if row['reads'] or row['events']:
                assert row['owned']
                assert all(BASE<=a<BASE+self.case[3]*self.case[4] for a in row['reads'])
            if row['ready']:assert row['owned'] and row['phase']==2
        return rows


@pytest.mark.parametrize('case',SUPPORTED,ids=case_id)
def test_write_and_key_change_use_one_owned_scan_without_hidden_reads(integrated,case):
    s=Script(case);s.add(begin(value(case,1)),status=2);s.scan('open')
    s.add('null_begin 8',status=3,phase=2);s.add('begin 1 0 42',status=3,phase=2)
    s.add(f'claim 0 {case[3]}',status=1,occupied=1)
    s.add(f'claim {case[3]*case[4]} {case[3]}',status=0,occupied=2,external=2);s.add('release 2',status=1,occupied=1)
    for n in [1,2,3]:s.update(value(case,n))
    if s.store.kind=='keys':
        s.add('get 7',status=1,answer=357)
        s.add('get 99',status=0,answer=357)
        s.add('put 13 99',status=3,phase=2)
        for command,key,new in [('put 7 42',7,42),('remove 11',11,None),('remove 7',7,None),('remove 99',99,None)]:
            s.store.change(key,new);s.add(command,status=0,phase=3)
            while s.store.active is not None:s.operation()
            s.scan()
    s.add('dump',memory=list(s.store.medium.data));s.add('end',status=1,owned=0,phase=0);s.add('peek',status=0,ready=0)
    s.check(integrated)


@pytest.mark.parametrize('case',UNSUPPORTED,ids=case_id)
def test_no_space_claims_nothing(integrated,case):
    s=Script(case);s.add('open',status=4,phase=0,owned=0,occupied=0);s.add(begin(value(case,1)),status=2);s.add('poll 4294967295',phase=0);s.add('step',phase=0);s.check(integrated)


REPRESENTATIVE=[case for case in SUPPORTED if case[0]=='CH32V003F4P6' and case[1]=='fast' and case[4]==4]
def operation_count(case):
    store=make(case);store.begin(value(case,1));return len(store.operations)
FAULTS=[(case,position,mask) for case in REPRESENTATIVE for position in range(operation_count(case)) for mask in [0,255,256]]


@pytest.mark.parametrize('case,position,mask',FAULTS,ids=lambda v:case_id(v) if isinstance(v,tuple) else str(v))
def test_partial_operation_failure_requires_rescan_and_keeps_lease(integrated,case,position,mask):
    s=Script(case);s.scan('open');s.update(value(case,1));s.store.begin(value(case,2));s.add(begin(value(case,2)),status=0)
    for _ in range(position):s.operation()
    s.operation(mask);s.add('peek',status=0,ready=0);s.add('step',phase=5,events=[])
    s.add('claim 0 64',status=1,occupied=1)
    # Recreate volatile planner state while retaining physical program history.
    c=case[6];s.store=formats.Store(s.store.medium,kind=c['kind'],policy=c['policy'],commit=c['commit'])
    s.scan();s.update(value(case,3),recover=True)
    s.add('dump',memory=list(s.store.medium.data));s.add('end',status=1,occupied=0);s.check(integrated)


@pytest.mark.parametrize('stage',['scanning','ready','writing','done','failed'])
def test_revoked_ticket_blocks_all_media_and_cannot_release_replacement(integrated,stage):
    case=REPRESENTATIVE[4];s=Script(case);s.add('open',status=0,ticket=1)
    if stage!='scanning':
        for _ in range(16):s.add('poll 32')
    if stage in ['writing','done','failed']:
        s.store.begin(value(case,1));s.add(begin(value(case,1)),status=0)
        if stage=='done':
            while s.store.active is not None:s.operation()
        elif stage=='failed':s.operation(0)
    s.add('revoke 1',status=1,owned=0,occupied=0);s.add('claim 0 256',status=0,external=2)
    s.add('peek',status=0,ready=0);s.add('put 7 42',status=8,phase=6)
    s.add('poll 32',phase=6,reads=[]);s.add('step',phase=6,events=[]);s.add('end',status=0,occupied=1)
    s.add('release 2',status=1,occupied=0);s.scan('open');s.add('end',status=1);s.check(integrated)


@pytest.mark.parametrize('case',REPRESENTATIVE,ids=case_id)
@pytest.mark.parametrize('budget',[1,7,0xffffffff])
def test_explicit_read_budget_zero_and_clamp(integrated,case,budget):
    s=Script(case);s.scan('open',budget);s.add('end',status=1)
    rows=s.check(integrated)
    assert sum(len(r['reads']) for r in rows)==(8 if case[6]['kind']=='raw' else case[3]*case[4])


def seed(s):
    s.add(f'claim 0 {s.case[3]*s.case[4]}',status=0,external=1)
    for i,byte in enumerate(s.store.medium.data):
        if byte!=255:s.add(f'poke {i} {byte}',status=1)
    s.add('release 1',status=1,occupied=0)


@pytest.mark.parametrize('case',[c for c in REPRESENTATIVE if c[6]['kind']!='raw'],ids=case_id)
@pytest.mark.parametrize('fault',['corrupt','exhausted'])
def test_unreadable_and_exhausted_cache_never_sneak_in_a_write(integrated,case,fault):
    s=Script(case);s.store.begin(value(case,1))
    while s.store.active is not None:s.store.step()
    m=s.store.medium.data
    if fault=='corrupt':m[12]^=1
    else:
        struct.pack_into('<I',m,4,0xffffffff)
        size=struct.unpack_from('<H',m,8)[0];crc=zlib.crc32(m[:12]+m[16:16+size])&0xffffffff
        struct.pack_into('<I',m,12,crc)
        if s.store.commit:struct.pack_into('<4sII',m,s.store.bank_bytes-s.store.footer_bytes,b'END1',0xffffffff,crc)
    seed(s);s.scan('open');status=5 if fault=='corrupt' else 7
    s.add(begin(value(case,2)),status=status,phase=2)
    if s.store.kind=='keys':s.add('put 7 42',status=status,phase=2);s.add('remove 7',status=status,phase=2)
    if fault=='corrupt':s.update(value(case,3),recover=True)
    else:s.add(begin(value(case,2),True),status=7,phase=2)
    s.add('end',status=1);s.check(integrated)


@pytest.mark.parametrize('case',[c for c in REPRESENTATIVE if c[6]['policy']=='two-bank'],ids=case_id)
def test_equal_generation_banks_refuse_even_explicit_recovery(integrated,case):
    s=Script(case);s.store.begin(value(case,1))
    while s.store.active is not None:s.store.step()
    n=s.store.bank_bytes;s.store.medium.data[n:2*n]=s.store.medium.data[:n]
    seed(s);s.scan('open');s.add(begin(value(case,2),True),status=6,phase=2)
    if s.store.kind=='keys':s.add('put 7 42',status=6);s.add('remove 7',status=6)
    s.add('end',status=1);s.check(integrated)
