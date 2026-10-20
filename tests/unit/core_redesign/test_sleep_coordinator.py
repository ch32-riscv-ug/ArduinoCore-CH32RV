"""Integrated lease/time invariants plus differential participant-list traces."""
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
INPUT = REPO/'planning/core-redesign/sleep-flow-inputs.json'
CONFIG = json.loads(INPUT.read_text())
VARIANTS = CONFIG['variants']


@pytest.fixture(scope='module')
def sleep_native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    exe = tmp_path_factory.mktemp('sleep-coordinator')/'control'
    flags = ['-std=c++17', '-O0', '-g1', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    command = [compiler, *flags, str(UNIT/'sleep_coordinator_driver.cpp'), '-o', str(exe)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert p.returncode == 0, p.stderr
    if destination := os.environ.get('EXP06_SLEEP_FLOW_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(exe, out/'native-control')
        sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
        paths = [UNIT/name for name in ['sleep_coordinator.hpp', 'sleep_coordinator_driver.cpp',
                 'test_sleep_coordinator.py', 'io_lifecycle.hpp', 'logical_time.hpp', 'deadline_timer.hpp']] + [INPUT]
        for source in paths:
            dest = out/'inputs'/source.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
        (out/'native-build.json').write_text(json.dumps(dict(command=command, compiler_sha256=sha(compiler),
            binary_sha256=sha(exe), stdout=p.stdout, stderr=p.stderr,
            inputs={str(p.relative_to(REPO)): sha(p) for p in paths}), indent=2)+'\n')
    return exe


def replay(exe, variant, commands, short=False):
    p = subprocess.run([str(exe), str(variant), 'short' if short else 'normal'],
                       input='\n'.join(commands)+'\n', capture_output=True, text=True, timeout=60)
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    assert len(rows) == len(commands)
    for cmd, row in zip(commands, rows):
        # A lease is never free just because cancel/close was requested.
        for port in row['ports']:
            assert port[3] == 1-port[7], (cmd, row)
        if row['phase'] == 4:
            assert row['clock'][0] == 2 and row['clock_ready'] == 0 and row['frozen'] == 1
            for registered, port in zip(row['registered'], row['ports']):
                if registered: assert port[:4] == [0, 0, 0, 0]
        if row['phase'] in (4, 5, 6):
            assert all(not port[1] for registered, port in zip(row['registered'], row['ports']) if registered), (cmd, row)
    return rows


def named(commands, rows, command, occurrence=0):
    return [row for cmd, row in zip(commands, rows) if cmd == command][occurrence]


@pytest.mark.parametrize('variant', VARIANTS)
@pytest.mark.parametrize('outcome', [1, 2, 3])
@pytest.mark.parametrize('known', [False, True])
def test_ten_seconds_cancel_collect_sleep_restore_and_deadline(sleep_native, variant, outcome, known):
    commands = ['init', 'observe 1 60000000 60000000', 'arm 1000000 1000000',
                'start 0 32', 'start 1 8', 'request', 'poll', 'start 0 1',
                f'finish 0 1 {outcome} 32 55 0', f'finish 0 1 {outcome} 32 55 1',
                'poll', 'read 0 0', 'take 0 1', 'read 0 0', 'poll',
                'finish 1 1 2 3 90 1', 'take 1 1', 'poll', 'enter 2 1 1 0',
                'enter 1 1 1 0', 'timer_poll', 'poll', 'start 0 1', 'wake 2', 'wake 1',
                'ready 0 1', 'ready 1 1', 'poll', f'restore 1 2 0 {int(known)} 2500000 0',
                'poll', 'timer_poll', 'timer_take', 'start 0 4']
    rows = replay(sleep_native, variant, commands)
    assert rows[1]['stamp'] == [10000000, 1, 0]
    assert rows[5]['phase'] == 2 and all(p[5] for p in rows[5]['ports'][:2])
    assert rows[8]['status'] == 0 and rows[9]['status'] == 1
    assert rows[10]['phase'] == 2 and rows[11]['read'] == 222
    assert rows[13]['read'] == 55 and rows[14]['phase'] == 2
    assert rows[17]['phase'] == 3 and rows[18]['status'] == 0
    assert rows[19]['status'] == 2 and rows[20]['status'] == 2  # no time source while paused
    assert rows[27]['phase'] == 5 and not rows[27]['clock_ready']
    assert rows[29]['phase'] == 1 and rows[-1]['status'] == 0
    if known:
        assert rows[28]['stamp'] == [12500000, 1, 0]
        assert rows[31]['notice'] == [12000000, 1, 0, 1]
    else:
        assert rows[28]['stamp'] == [0, 2, 0]
        assert rows[30]['status'] == 5 and rows[31]['status'] == 0 and not rows[30]['timer'][0]


@pytest.mark.parametrize('variant', VARIANTS)
@pytest.mark.parametrize('injection', [1, 2, 3])
def test_input_around_final_check_stays_sticky(sleep_native, variant, injection):
    commands = ['init', 'request', 'poll', f'enter 1 1 1 {injection}', 'start 0 4', 'poll']
    if injection != 1:
        commands += ['ready 0 1', 'ready 1 1', 'restore 1 2 0 0 0 0', 'poll']
    commands += ['input_take', 'input_take']
    rows = replay(sleep_native, variant, commands)
    if injection == 1:
        assert rows[3]['status'] == 0 and rows[3]['phase'] == 1 and rows[3]['clock'][0] == 1
        assert rows[4]['status'] == 0
    else:
        assert rows[3]['status'] == 3 and rows[3]['phase'] == 5 and rows[4]['status'] == 4
        assert rows[-3]['phase'] == 1
    assert rows[-2]['status'] == 1 and rows[-1]['status'] == 0


@pytest.mark.parametrize('variant', VARIANTS)
def test_input_aborts_preparation_without_undoing_cancel(sleep_native, variant):
    commands = ['init', 'start 0 4', 'request', 'post', 'poll', 'read 0 0',
                'request', 'input_take', 'finish 0 1 1 4 80 1', 'read 0 0',
                'take 0 1', 'read 0 0', 'start 0 4']
    rows = replay(sleep_native, variant, commands)
    assert rows[4]['phase'] == 1 and rows[4]['ports'][0][3:6] == [1, 0, 1]
    assert rows[6]['status'] == 0 and rows[9]['status'] == 0
    assert rows[11]['read'] == 80 and rows[-1]['status'] == 0


@pytest.mark.parametrize('variant', VARIANTS)
@pytest.mark.parametrize('wake,gate', [(0, 1), (1, 0), (0, 0)])
def test_no_wake_or_atomic_gate_is_not_a_sleep(sleep_native, variant, wake, gate):
    commands = ['init', 'request', 'poll', f'enter 1 {wake} {gate} 0', 'poll', 'abort']
    rows = replay(sleep_native, variant, commands)
    assert rows[3]['status'] == 1 and rows[3]['phase'] == 3
    assert rows[3]['clock'][0] == 1 and all(p[0] for p in rows[3]['ports'][:2])
    assert rows[-1]['phase'] == 1 and not rows[-1]['frozen']


@pytest.mark.parametrize('variant', VARIANTS)
def test_partial_restore_failure_and_retry_admit_nothing(sleep_native, variant):
    commands = ['init', 'request', 'poll', 'enter 1 1 1 0', 'post',
                'restore 2 2 0 0 0 0', 'restore 1 0 0 0 0 0', 'poll',
                'restore 1 2 0 0 0 0', 'ready 0 1', 'poll', 'start 0 1',
                'ready 1 2', 'poll', 'start 0 1', 'restore 1 2 0 1 1 0',
                'ready 1 1', 'retry', 'poll', 'input_take', 'start 0 1']
    rows = replay(sleep_native, variant, commands)
    assert rows[5]['status'] == 3 and rows[6]['status'] == 5
    assert rows[10]['ports'][0][:3] == [1, 0, 1] and rows[10]['ports'][1][:3] == [0, 0, 0]
    assert rows[11]['status'] == rows[14]['status'] == 4
    assert rows[13]['phase'] == 6 and rows[15]['status'] == 3
    assert rows[18]['phase'] == 1 and rows[19]['status'] == 1 and rows[-1]['status'] == 0


@pytest.mark.parametrize('variant', VARIANTS)
def test_deadline_notification_prevents_entry_and_survives_abort(sleep_native, variant):
    commands = ['init', 'arm 10 0', 'observe 1 60 60', 'timer_poll', 'request',
                'timer_take', 'request', 'poll', 'arm 0 0', 'timer_poll', 'enter 1 1 1 0',
                'timer_take', 'request', 'poll', 'enter 1 1 1 0', 'enter 2 1 1 0']
    rows = replay(sleep_native, variant, commands)
    assert rows[4]['status'] == 0 and rows[4]['out_attempt'] == 0
    assert rows[10]['status'] == 0 and rows[10]['phase'] == 1 and rows[10]['timer'][1]
    assert rows[14]['status'] == 0 and rows[15]['status'] == 2


@pytest.mark.parametrize('variant', VARIANTS)
def test_attempts_retire_without_recycling(sleep_native, variant):
    commands = ['init']
    for _ in range(255): commands += ['request', 'abort']
    commands += ['request', 'poll', 'enter 1 1 1 0']
    rows = replay(sleep_native, variant, commands, short=True)
    assert rows[-3]['status'] == 0 and rows[-3]['out_attempt'] == 255 and rows[-1]['status'] == 0


@pytest.mark.parametrize('variant', [2, 4])
def test_registry_full_duplicate_busy_stale_and_frozen(sleep_native, variant):
    commands = ['init', 'add 0', 'start 0 4', 'remove 1 0', 'request', 'remove 2 1',
                'add 2', 'post', 'poll', 'input_take', 'finish 0 1 3 0 0 1', 'take 0 1',
                'remove 1 0', 'add 2', 'remove 1 0', 'request', 'poll']
    rows = replay(sleep_native, variant, commands)
    assert rows[1]['status'] == rows[3]['status'] == rows[5]['status'] == rows[6]['status'] == 0
    assert rows[12]['status'] == rows[13]['status'] == 1 and rows[13]['registration'] == [3, 0]
    assert rows[14]['status'] == 0 and rows[-1]['phase'] == 3


def test_registry_capacity_and_retirement(sleep_native):
    rows = replay(sleep_native, 2, ['init', 'add 2'])
    assert rows[-1]['status'] == 0 and rows[-1]['registration'] == [2, 1]
    rows = replay(sleep_native, 4, ['init', 'add 2', 'add 3', 'add 4'])
    assert rows[-1]['status'] == 0 and rows[-1]['count'] == 4 and rows[-1]['registration'] == [4, 3]
    commands = ['init', 'remove 1 0']
    for g in range(3, 256): commands += ['add 0', f'remove {g} 0']
    commands += ['add 0']
    rows = replay(sleep_native, 2, commands, short=True)
    assert rows[-1]['status'] == 0 and rows[-1]['registration'] == [255, 0]


@pytest.mark.parametrize('variant', [2, 4])
def test_unlisted_resource_is_outside_the_guarantee(sleep_native, variant):
    commands = ['init', 'remove 2 1', 'start 1 4', 'request', 'poll', 'enter 1 1 1 0']
    rows = replay(sleep_native, variant, commands)
    assert rows[-1]['status'] == 2 and not rows[-1]['registered'][1]
    assert rows[-1]['ports'][1][:4] == [1, 1, 1, 1]
    # This counterexample rejects the claim that a manager discovers all users.


@pytest.mark.parametrize('variant', VARIANTS)
def test_elapsed_overflow_cannot_restore_or_admit_io(sleep_native, variant):
    commands = ['init', 'observe 1 60000000 60000000', 'request', 'poll', 'enter 1 1 1 0',
                'wake 1', f'restore 1 2 0 1 {2**64-1} 0', 'ready 0 1', 'ready 1 1',
                'poll', 'start 0 4', 'restore 1 2 0 0 0 0', 'poll']
    rows = replay(sleep_native, variant, commands)
    assert rows[6]['status'] == 12 and rows[6]['stamp'] == [10000000, 1, 0]
    assert rows[9]['phase'] == 5 and rows[10]['status'] == 4 and rows[-1]['phase'] == 1


@pytest.mark.parametrize('seed', CONFIG['trace_seeds'])
def test_same_serial_stimuli_for_explicit_and_registry(sleep_native, seed):
    rng = random.Random(0x51ee+seed)
    commands = ['init', 'observe 1 60000000 60000000']
    for _ in range(CONFIG['trace_operations']):
        i = rng.randrange(2)
        commands.append(rng.choice(['request', 'poll', 'post', 'input_take', 'abort', 'retry',
            f'start {i} 4', f'finish {i} {rng.randrange(1,10)} 3 0 0 1', f'take {i} {rng.randrange(1,10)}',
            f'ready {i} {rng.randrange(3)}', f'enter {rng.randrange(1,10)} 1 1 {rng.randrange(4)}',
            f'wake {rng.randrange(1,10)}', f'restore {rng.randrange(1,10)} 2 0 0 0 0', 'timer_poll', 'timer_take']))
    traces = [replay(sleep_native, v, commands) for v in VARIANTS]
    for rows in traces:
        for row in rows: row.pop('registration')
    assert traces[0] == traces[1] == traces[2]
