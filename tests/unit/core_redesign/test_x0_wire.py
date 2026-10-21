"""Whole X0 wire composition: independent packet/reply ledger, no hardware."""
import gzip
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
spec = importlib.util.spec_from_file_location('x0_wire_reference', UNIT/'x0_wire_oracle.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def wire_native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    dest = tmp_path_factory.mktemp('x0-wire')
    command = [compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror',
               '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
               str(UNIT/'x0_wire_driver.cpp'),'-o',str(dest/'app')]
    result = subprocess.run(command,capture_output=True,text=True,timeout=240)
    assert result.returncode == 0, result.stderr
    if output := os.environ.get('EXP06_X0_WIRE_EVIDENCE'):
        out = Path(output)
        out.mkdir(parents=True,exist_ok=False)
        shutil.copy2(dest/'app',out/'app')
        sources = [UNIT/name for name in [
            'x0_wire.hpp','x0_wire_driver.cpp','x0_wire_oracle.py','test_x0_wire.py',
            'x0_features.hpp','x0_candidates.hpp','display_parts.hpp','r_application.hpp','r_workloads.hpp',
            'data_parts.hpp','work_executor.hpp','event_executor.hpp','static_mailbox.hpp',
            'app_oracle.py','oracle.py','frame-golden.json']]
        sources += [REPO/'planning/core-redesign/x0-wire-inputs.json', REPO/'planning/core-redesign/workloads.ja.md']
        for source in sources:
            target = out/'inputs'/source.relative_to(REPO)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,target)
        (out/'build.json').write_text(json.dumps(dict(command=command,returncode=result.returncode,
            stdout=result.stdout,stderr=result.stderr,compiler_sha256=sha(compiler),
            compiler_version=subprocess.check_output([compiler,'--version'],text=True),
            executable_sha256=sha(dest/'app'),inputs={str(p.relative_to(REPO)):sha(p) for p in sources}),indent=2)+'\n')
    return dest/'app'


def ticks(first,last,opportunity='step'):
    return [command for time in range(first,last) for command in [f'time {time}',opportunity]]


def expected(commands,domain=0,opted=False):
    model = reference.Model()
    rows = []
    for command in commands:
        if command.startswith('opportunity '):
            where,profile,context = map(int,command.split()[1:])
            allowed = context == 0 and (where == 0 or (profile != 0 and (domain == 0 or (profile == 2 and opted))))
            row = model.apply('step') if allowed else model.observe()
        else:
            row = model.apply(command)
        rows.append(row)
    return rows,model


def replay(executable,candidate,commands,domain=0,opted=False,target=None):
    result = subprocess.run([str(executable),candidate,str(domain),str(int(opted))],
                            input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if target is None:
        target,_ = expected(commands,domain,opted)
    assert len(rows) == len(target)
    for index,(actual,wanted) in enumerate(zip(rows,target)):
        assert actual == wanted, (candidate,index,commands[index],actual,wanted)
        assert actual['started']-actual['reclaimed'] == actual['active']
        assert actual['completed']-actual['reclaimed'] == actual['terminal']
        assert actual['corruptions'] == 0
    if output := os.environ.get('EXP06_X0_WIRE_EVIDENCE'):
        key = hashlib.sha256(json.dumps([candidate,domain,opted,commands]).encode()).hexdigest()
        Path(output,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(dict(candidate=candidate,
            domain=domain,opted=opted,commands=commands,rows=rows),separators=(',',':')).encode(),mtime=0))
    return rows


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_same_application_with_commands_and_true_receive_slots(wire_native,candidate):
    commands = ['attach','key 1','key 0',*ticks(0,3000)]
    target,model = expected(commands)
    rows = replay(wire_native,candidate,commands,target=target)
    assert rows[-1]['confirmed'] == 5020 and rows[-1]['mv'] == 5000 and rows[-1]['ma'] == 1000
    assert rows[-1]['measure_done'] == 30 and rows[-1]['touch_done'] == 150
    assert rows[-1]['frames'] >= 2 and rows[-1]['revision'] == rows[-1]['displayed_revision']
    assert all(t['rx_size'] == 2 and t['payload'] in [b'\x02',b'\x04'] for t in model.ledger if t['owner'] == 2)
    assert all(t['kind'] == 1 and t['address'] == 0 for t in model.ledger if t['owner'] == 3)


PREFIX = {
    'command': ['fault 1','step'],
    'data': ['step','time 2','step','time 4','step','fault 1','time 6','step'],
    'bus-word': ['step','fault 2','time 2','step'],
    'current-word': ['step','time 2','step','time 4','step','time 6','step','fault 2','time 8','step'],
}


@pytest.mark.parametrize('candidate',['R','A','B','C'])
@pytest.mark.parametrize('phase',list(PREFIX))
def test_failure_in_each_subtransaction_and_recovery(wire_native,candidate,phase):
    commands = [*PREFIX[phase],*ticks(10,3000)]
    target,model = expected(commands)
    replay(wire_native,candidate,commands,target=target)
    failed = [t for t in model.ledger if t['outcome'] is False]
    assert len(failed) == 1
    if phase in ['command','data']:
        assert failed[0]['payload'][0] == (0 if phase == 'command' else 0x40)
        assert model.frames > 0
    else:
        assert failed[0]['payload'] == bytes([2 if phase == 'bus-word' else 4])
        first_done = next(row for row in target if row['measure_done'])
        assert first_done['stale'] == 1 and first_done['mv'] == first_done['ma'] == 0
        assert model.pd.stale is False and model.pd.mv == 5000


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_receive_at_completion_pair_failure_preserves_previous_value(wire_native,candidate):
    commands = [*ticks(0,80),'raw 14000 65506','fault 2',*ticks(80,160),
                *ticks(160,280),'raw 18000 32768',*ticks(280,390)]
    target,model = expected(commands)
    rows = replay(wire_native,candidate,commands,target=target)
    assert any(row['mv'] == 5000 and row['ma'] == 1000 and row['stale'] for row in rows[160:])
    assert any(row['mv'] == 7000 and row['ma'] == -30 and not row['stale'] for row in rows)
    assert rows[-1]['mv'] == 9000 and rows[-1]['ma'] == -32768
    assert len(model.measurement_data) == 8


@pytest.mark.parametrize('domain,opted',[(0,False),(1,False),(1,True)])
def test_same_progress_permissions_and_long_stall(wire_native,domain,opted):
    commands = ['attach','key 1','key 0','step','time 2']
    commands += [f'opportunity 0 2 {context}' for context in [1,2,3]]
    for where in [1,2]:
        for profile in [0,1,2]:
            commands += [f'opportunity {where} {profile} 0']
    commands += ['time 1000','step',*ticks(1001,2500)]
    target,_ = expected(commands,domain,opted)
    for candidate in ['R','A','B','C']:
        replay(wire_native,candidate,commands,domain,opted,target)


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_cancel_pending_completion_sessions_and_full_input(wire_native,candidate):
    commands = ['attach','key 1','key 0','key 1','key 0','key 1','step','cancel','cancel',
                'time 10','detach','attach','finish 1 1 1','key 0',*ticks(11,3000)]
    rows = replay(wire_native,candidate,commands)
    assert rows[-1]['accepted'] == 5 and rows[-1]['rejected'] == 1
    assert rows[-1]['confirmed'] == 5040 and rows[-1]['valid']


@pytest.mark.parametrize('seed',range(10))
def test_generated_input_and_wire_faults(wire_native,seed):
    rng = random.Random(seed)
    commands = []
    now = 0
    for _ in range(10000):
        kind = rng.randrange(12)
        if kind == 0:
            commands.append('attach')
        elif kind == 1:
            commands.append('detach')
        elif kind <= 3:
            commands.append(f'key {rng.choice([-1,0,1])}')
        elif kind == 4:
            now += rng.randrange(30)
            commands.append(f'time {now}')
        elif kind == 5:
            commands.append(f'fault {rng.randint(1,3)}')
        elif kind == 6:
            commands.append(f'raw {rng.randrange(1000,2200)*8} {rng.randrange(65536)}')
        elif kind == 7:
            commands.append(f'opportunity {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}')
        elif kind == 8:
            commands.append('cancel')
        else:
            commands.append('step')
    commands += ticks(now+1,now+2001)
    target,_ = expected(commands)
    for candidate in ['R','A','B','C']:
        replay(wire_native,candidate,commands,target=target)


def test_transport_acceptance_copy_receive_and_sticky_terminal(wire_native):
    result = subprocess.run([str(wire_native),'contracts'],capture_output=True,text=True,timeout=30)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_two_register_reads_are_not_a_coherent_or_submit_time_sample(wire_native,candidate):
    commands = ['step','time 2','step','raw 14000 2000','time 4',
                'raw 18000 3000','step','time 6','step','time 8','step',
                'raw 22000 65506','time 10','raw 26000 4000','step']
    rows = replay(wire_native,candidate,commands)
    assert rows[-1]['raw_words'] == [14000,65506]
    assert rows[-1]['mv'] == 7000 and rows[-1]['ma'] == -30
    assert rows[-1]['measure_done'] == 1 and not rows[-1]['stale']
    # This only says both transport operations succeeded. It does not establish
    # analog quality, freshness or simultaneous conversion of these registers.
