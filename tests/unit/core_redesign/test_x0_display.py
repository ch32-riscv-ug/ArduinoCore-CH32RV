"""Display replacement against whole images and independent transaction ledgers."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
INPUT = REPO/'planning/core-redesign/display-inputs.json'
CONFIG = json.loads(INPUT.read_text())
spec = importlib.util.spec_from_file_location('display_reference',UNIT/'display_oracle.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def display_native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    dest = tmp_path_factory.mktemp('x0-display')
    flags = ['-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    builds = []
    for source, label in [('x0_display_driver.cpp','app'),('display_library_driver.cpp','library')]:
        extra = []
        source_path = UNIT/source
        if label == 'library':
            # Prove the library builds after copying it outside the experiment
            # include graph, without access to C++ standard-library headers.
            isolated = dest/'isolated'
            isolated.mkdir()
            for name in ['display_library_driver.cpp','display_parts.hpp']: shutil.copy2(UNIT/name,isolated/name)
            source_path = isolated/source
            extra = ['-nostdinc++']
        command = [compiler,*flags,*extra,str(source_path),'-o',str(dest/label)]
        p = subprocess.run(command,capture_output=True,text=True,timeout=240)
        assert p.returncode == 0, p.stderr
        builds.append(dict(command=command,stdout=p.stdout,stderr=p.stderr))
    if destination := os.environ.get('EXP05_DISPLAY_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True,exist_ok=False)
        for label in ['app','library']: shutil.copy2(dest/label,out/label)
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        paths = [UNIT/name for name in ['display_parts.hpp','x0_display.hpp','x0_display_driver.cpp','display_library_driver.cpp',
                 'display_oracle.py','test_x0_display.py','x0_features.hpp','x0_candidates.hpp','r_workloads.hpp',
                 'app_oracle.py','oracle.py','data_parts.hpp','work_executor.hpp','event_executor.hpp',
                 'r_application.hpp','static_mailbox.hpp']]+[INPUT]
        for source in paths:
            target = out/'inputs'/source.relative_to(REPO)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,target)
        (out/'native-build.json').write_text(json.dumps(dict(builds=builds,compiler_sha256=sha(compiler),
            binaries={label:sha(dest/label) for label in ['app','library']},inputs={str(p.relative_to(REPO)):sha(p) for p in paths}),indent=2)+'\n')
    return dest


def ticks(first,last,opportunity='step'):
    return [command for tick in range(first,last) for command in (f'time {tick}',opportunity)]


def expected(panel,commands,domain=0,opted=False):
    model = reference.Model(panel)
    rows = []
    for command in commands:
        if command.startswith('opportunity '):
            where,profile,context = map(int,command.split()[1:])
            allowed = context == 0 and (where == 0 or (profile != 0 and (domain == 0 or (profile == 2 and opted))))
            row = model.apply('step') if allowed else model.observe()
        else: row = model.apply(command)
        rows.append(row)
    return rows


def replay(dest,candidate,panel,commands,domain=0,opted=False,target=None):
    p = subprocess.run([str(dest/'app'),candidate,str(panel),str(domain),str(int(opted))],input='\n'.join(commands)+'\n',
                       capture_output=True,text=True,timeout=90)
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    if target is None: target = expected(panel,commands,domain,opted)
    assert len(rows) == len(target)
    for i,(row,wanted) in enumerate(zip(rows,target)):
        assert row == wanted, (candidate,panel,i,commands[i],row,wanted)
        assert row['started']-row['reclaimed'] == row['active']
        assert row['completed']-row['reclaimed'] == row['terminal']
        assert row['corruptions'] == 0
    return rows


@pytest.mark.parametrize('panel',range(4),ids=[p['id'] for p in CONFIG['panels']])
def test_independent_library_and_full_image(panel,display_native):
    p = subprocess.run([str(display_native/'library'),str(panel)],capture_output=True,text=True,timeout=30)
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    row = json.loads(p.stdout)
    profile = CONFIG['panels'][panel]
    first = (5020,5000,5070,1200,True,False,True)
    next_view = (9000,0,-1,0,False,True,False)
    assert row['bytes'] == profile['bytes'] and row['capacity'] == profile['chunk']
    assert row['chunks'] == (profile['bytes']+profile['chunk']-1)//profile['chunk']
    assert row['tail'] == (profile['bytes']-1)%profile['chunk']+1
    assert row['first_hash'] == reference.base.fnv(reference.image(panel,first))
    assert row['next_hash'] == reference.base.fnv(reference.image(panel,next_view))


@pytest.mark.parametrize('candidate',CONFIG['candidates'])
@pytest.mark.parametrize('panel',range(4))
def test_same_x0_workload_with_different_displays(display_native,candidate,panel):
    commands = ['attach','key 1','key 0',*ticks(0,CONFIG['baseline_ticks'])]
    rows = replay(display_native,candidate,panel,commands)
    assert rows[-1]['confirmed'] == 5020 and rows[-1]['measure_done'] == 30 and rows[-1]['touch_done'] == 150
    assert rows[-1]['frames'] == 2 and rows[-1]['revision'] == rows[-1]['displayed_revision']


@pytest.mark.parametrize('candidate',CONFIG['candidates'])
@pytest.mark.parametrize('panel',range(4))
def test_faults_stall_sessions_and_frozen_view(display_native,candidate,panel):
    commands = ['attach','key 1','key 0',*ticks(0,15),'source 7000 1200','key 1','key 0','fault 1',
                *ticks(15,65),'fault 2','fault 3','detach','attach','finish 1 1 1','key 0',
                'time 500','step',*ticks(501,4500)]
    rows = replay(display_native,candidate,panel,commands)
    assert rows[-1]['rejected'] == 0 and rows[-1]['confirmed'] >= 5000


@pytest.mark.parametrize('panel',range(4))
@pytest.mark.parametrize('domain,opted',[(0,False),(1,False),(1,True)])
def test_explicit_and_automatic_opportunity_gates(display_native,panel,domain,opted):
    commands = ['attach','key 1','key 0','opportunity 0 2 1','opportunity 0 2 2','opportunity 0 2 3']
    time = 0
    for where in [1,2]:
        for profile in [0,1,2]:
            commands += ticks(time,time+15,f'opportunity {where} {profile} 0')
            time += 15
    commands += ticks(time,time+3500)
    target = expected(panel,commands,domain,opted)
    for candidate in CONFIG['candidates']: replay(display_native,candidate,panel,commands,domain,opted,target)


@pytest.mark.parametrize('seed',CONFIG['seeds'])
def test_generated_input_for_all_panels_and_execution_candidates(display_native,seed):
    rng = random.Random(0xd150+seed)
    commands = []; time = 0
    for _ in range(CONFIG['generated_operations']):
        choice = rng.randrange(9)
        if choice == 0: commands.append('attach')
        elif choice == 1: commands.append('detach')
        elif choice == 2: commands.append(f'key {rng.choice([-1,0,1])}')
        elif choice == 3: time += rng.randrange(20); commands.append(f'time {time}')
        elif choice == 4: commands.append(f'fault {rng.randrange(1,4)}')
        elif choice == 5: commands.append(f'source {rng.randrange(-10,11001)} {rng.randrange(3001)}')
        elif choice == 6: commands.append(f'opportunity {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}')
        else: commands.append('step')
    commands += ticks(time+1,time+CONFIG['generated_drain_ticks']+1)
    for panel in range(4):
        target = expected(panel,commands)
        for candidate in CONFIG['candidates']: replay(display_native,candidate,panel,commands,target=target)


@pytest.mark.parametrize('panel',range(4))
def test_borrowed_buffer_is_not_reprepared_and_corruption_probe_is_live(display_native,panel):
    p = subprocess.run([str(display_native/'app'),'R',str(panel),'0','0'],input='step\nprobe_prepare\ntime 1\ncorrupt\ntime 2\n',
                       capture_output=True,text=True,timeout=30)
    assert p.returncode == 0, p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    assert rows[2]['corruptions'] == 0 and rows[-1]['corruptions'] == 1
