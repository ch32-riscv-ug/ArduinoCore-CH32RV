"""Logical time compared with rational arithmetic and non-wrapping deadlines."""
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
spec=importlib.util.spec_from_file_location('logical_reference',UNIT/'logical_time_oracle.py');reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)
CONFIG=json.loads((REPO/'planning/core-redesign/logical-time-inputs.json').read_text());VARIANTS=CONFIG['variants']


@pytest.fixture(scope='module')
def logical_native(tmp_path_factory):
    compiler=shutil.which('g++');assert compiler
    exe=tmp_path_factory.mktemp('logical-time')/'control'
    flags=['-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    p=subprocess.run([compiler,*flags,str(UNIT/'logical_time_driver.cpp'),'-o',str(exe)],capture_output=True,text=True,timeout=300)
    assert p.returncode==0,p.stderr
    if destination:=os.environ.get('EXP01_TIME_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False);shutil.copy2(exe,out/'native-control')
        sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
        paths=[UNIT/'logical_time_driver.cpp',UNIT/'logical_time.hpp',UNIT/'deadline_timer.hpp',UNIT/'logical_time_oracle.py',Path(__file__),REPO/'planning/core-redesign/logical-time-inputs.json']
        (out/'native-build.json').write_text(json.dumps(dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,compiler=compiler,compiler_sha256=sha(compiler),flags=flags,binary_sha256=sha(exe),inputs={str(p.relative_to(REPO)):sha(p) for p in paths}),indent=2)+'\n')
    return exe


def replay(exe,variant,commands,short=False):
    p=subprocess.run([str(exe),str(variant),'short' if short else 'normal'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
    assert p.returncode==0,p.stderr;assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];assert len(rows)==len(commands)
    model=reference.Model(VARIANTS[variant],short)
    for i,(command,row) in enumerate(zip(commands,rows)):assert row==model.apply(command),(variant,i,command,row,model.apply('state'))
    return rows


@pytest.mark.parametrize('variant',range(6),ids=[v['id'] for v in VARIANTS])
@pytest.mark.parametrize('rate',[3,7,1000,32768,6000000,48000000,4294967295])
def test_fraction_carry_and_many_raw_wraps(logical_native,variant,rate):
    v=VARIANTS[variant]
    if v['fixed_six'] and rate!=6000000:
        rows=replay(logical_native,variant,[f'begin 1 0 {rate} 0']);assert rows[0]['status']==5;return
    absolute=0;commands=[f'begin 1 0 {rate} 0']
    for n in range(100):
        delta=1 if n<12 else 999999937;absolute+=delta
        raw=absolute%(1<<v['bits'])
        commands.append(f'observe 1 {raw} {rate} 1 {delta}')
    rows=replay(logical_native,variant,commands)
    assert rows[-1]['us']==absolute*1000000//rate
    if rate==6000000:assert rows[6]['us']==1


@pytest.mark.parametrize('variant',range(6),ids=[v['id'] for v in VARIANTS])
def test_rejections_and_output_preservation(logical_native,variant):
    commands=['observe 1 0 6000000 1 0','pause','begin 0 0 6000000 0','begin 1 0 0 0','begin 1 0 6000000 100',
              'begin 1 0 6000000 0','observe 2 6 6000000 1 6','observe 1 6 48000000 1 6','observe 1 6 6000000 1 5',
              'pause','observe 1 6 6000000 1 6','known 2 0 6000000 20 0','observe 2 6 6000000 1 6','stop','observe 2 6 6000000 1 0']
    rows=replay(logical_native,variant,commands)
    assert rows[6]['out']==rows[5]['out']==rows[8]['out']
    assert rows[-1]['status']==3


@pytest.mark.parametrize('variant',[0,1,3,4])
def test_declared_bound_assumption_and_unobservable_extra_revolution(logical_native,variant):
    rows=replay(logical_native,variant,['begin 1 4294967290 6000000 0','observe 1 0 6000000 1 6',
          'observe 1 12 6000000 0 0','observe 1 18 6000000 1 4294967296','observe 1 18 6000000 1 100',
          'pause','unknown 2 0 6000000 0','observe 2 6 6000000 1 6'])
    assert rows[1]['us']==1 and rows[3]['status']==9 and rows[-1]['quality']==0
    if VARIANTS[variant]['assumed']:assert rows[2]['status']==1 and rows[4]['quality']==1
    else:assert rows[2]['status']==8 and rows[4]['quality']==0
    # Same observed endpoints can hide a whole revolution; the declared bound
    # is not verified by subtraction, even if the implementation returns Ready.
    actual_short=6;actual_long=(1<<32)+6
    assert actual_short%2**32==actual_long%2**32 and actual_short//6!=actual_long//6


@pytest.mark.parametrize('variant',[2,5])
def test_native64_backwards_and_large_deltas(logical_native,variant):
    rows=replay(logical_native,variant,['begin 1 4294967290 6000000 0','observe 1 8589934600 6000000 0 0','observe 1 1 6000000 0 0'])
    assert rows[1]['us']==(8589934600-4294967290)//6 and rows[-1]['status']==11 and rows[-1]['out']==rows[-2]['out']


@pytest.mark.parametrize('variant',[0,1,2])
def test_rate_boundary_requires_explicit_fraction_loss(logical_native,variant):
    rows=replay(logical_native,variant,['begin 1 0 3 0','observe 1 1 3 1 1','pause','known 2 0 7 100 0',
           'known 2 0 7 100 1','observe 2 7 7 1 7','arm 0 10 0','arm 1 10 0'])
    assert rows[3]['status']==14 and rows[3]['drop']==[55,66]
    assert rows[4]['drop']==[1,3] and rows[4]['quality']==2 and rows[-2]['status']==0 and rows[-1]['status']==1


@pytest.mark.parametrize('variant',range(6),ids=[v['id'] for v in VARIANTS])
def test_known_and_unknown_pause_domains_gate_timers(logical_native,variant):
    rows=replay(logical_native,variant,['begin 1 0 6000000 0','arm 0 2 1','observe 1 12 6000000 1 12','poll 0',
            'pause','known 2 0 6000000 10 0','poll 0','pause','unknown 3 0 6000000 0','poll 0','take 0','take 0'])
    assert rows[3]['strict'][1]==1 and rows[6]['status']==0 and rows[9]['status']==5
    assert rows[10]['notice'][3]==1 and rows[10]['status']==1 and rows[11]['status']==0


def test_timer_gap_backward_quality_and_saturation(logical_native):
    commands=['begin 1 0 6000000 0','arm 0 0 1']
    for i in range(1,5):commands += [f'stamp {i*((1<<31)-1)} 1 0 0','poll 0']
    commands += ['take 0',f'stamp {4*((1<<31)-1)+(1<<31)} 1 0 0','poll 0','take 0',
                 'stamp 100 1 0 0','arm 0 10 0','stamp 99 1 0 0','poll 0',
                 'stamp 100 1 1 1','arm 0 10 0','arm 1 10 0','stamp 110 1 1 8','poll 1','stamp 110 1 1 1','poll 1','take 1']
    rows=replay(logical_native,0,commands)
    assert rows[10]['notice'][1:3]==[0xffffffff,1]
    assert rows[12]['status']==7 and rows[17]['status']==6
    assert rows[-4]['status']==2 and rows[-2]['status']==0 and rows[-1]['notice'][3]==1


@pytest.mark.parametrize('variant',range(6),ids=[v['id'] for v in VARIANTS])
def test_overflow_and_epoch_retirement(logical_native,variant):
    rows=replay(logical_native,variant,[f'begin 1 0 6000000 {2**64-1}','observe 1 6 6000000 1 6','pause','known 2 0 6000000 1 0','unknown 2 0 6000000 0'])
    assert rows[1]['status']==12 and rows[3]['status']==12 and rows[4]['epoch']==2
    commands=[]
    for epoch in range(1,256):commands += ['begin 1 0 6000000 0','stop']
    commands += ['begin 1 0 6000000 0']
    rows=replay(logical_native,variant,commands,True);assert rows[-1]['status']==13 and rows[-1]['epoch']==255


@pytest.mark.parametrize('seed',range(8))
@pytest.mark.parametrize('variant',range(6),ids=[v['id'] for v in VARIANTS])
def test_generated_rational_trace(logical_native,variant,seed):
    rng=random.Random(0xc100+seed);v=VARIANTS[variant];hz=6000000 if v['fixed_six'] else rng.choice(CONFIG['rates_for_sensitivity'])
    absolute=0;source=1;commands=[f'begin {source} 0 {hz} 0']
    for _ in range(400):
        delta=rng.choice([0,1,5,6,999,1000000,999999937]);absolute+=delta;raw=absolute%(1<<v['bits'])
        commands += [f'observe {source} {raw} {hz} 1 {delta}']
        if rng.randrange(20)==0:
            source+=1;commands += ['pause',f'known {source} 0 {hz} {rng.randrange(1000)} 0'];absolute=0
        if rng.randrange(7)==0:commands += ['arm 1 1000 1000','poll 1','take 1']
    replay(logical_native,variant,commands)


@pytest.mark.parametrize('variant',[2,5])
def test_full_width_delta_and_intermediate_overflow(logical_native,variant):
    rows=replay(logical_native,variant,[f'begin 1 0 6000000 0',f'observe 1 {2**64-1} 6000000 0 0','observe 1 0 6000000 0 0'])
    assert rows[1]['us']==(2**64-1)//6 and rows[-1]['status']==11
    if variant==2:
        rows=replay(logical_native,variant,['begin 1 0 1 0',f'observe 1 {2**64-1} 1 0 0','observe 1 4 1 0 0'])
        assert rows[1]['status']==12 and rows[1]['raw']==0 and rows[1]['out']==rows[0]['out']
        assert rows[-1]['us']==4000000


@pytest.mark.parametrize('variant',[0,3])
def test_unknown_resume_retires_epoch_but_known_resume_can_continue(logical_native,variant):
    commands=['begin 1 0 6000000 0']
    for epoch in range(2,256):commands += ['pause',f'unknown {epoch} 0 6000000 0']
    commands += ['pause','unknown 256 0 6000000 0','known 256 0 6000000 5 0']
    rows=replay(logical_native,variant,commands,True)
    assert rows[-2]['status']==13 and rows[-2]['epoch']==255 and rows[-2]['phase']==2
    assert rows[-1]['status']==0 and rows[-1]['epoch']==255 and rows[-1]['us']==5


def test_stopped_native_counter_cannot_reveal_wall_time(logical_native):
    rows=replay(logical_native,2,['begin 1 0 6000000 0','observe 1 0 6000000 0 0','pause','unknown 2 0 6000000 0'])
    # Both no elapsed time and ten seconds with the source stopped can produce
    # this same raw pair. A new domain does not manufacture the missing duration.
    assert rows[1]['us']==0 and rows[1]['status']==0 and rows[-1]['epoch']==2 and rows[-1]['us']==0
