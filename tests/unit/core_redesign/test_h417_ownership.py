"""Serial endpoint policies and recovery against a separate list ledger."""
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
INPUT = REPO/'planning/core-redesign/h417-ownership-inputs.json'
CONFIG = json.loads(INPUT.read_text())
spec = importlib.util.spec_from_file_location('h417_reference', UNIT/'h417_ownership_oracle.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    exe = tmp_path_factory.mktemp('h417-ownership')/'control'
    flags = ['-std=c++17', '-O1', '-g1', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    command = [compiler, *flags, str(UNIT/'h417_ownership_driver.cpp'), '-o', str(exe)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert p.returncode == 0, p.stderr
    if destination := os.environ.get('EXP05_H417_OWNERSHIP_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(exe, out/'native-control')
        paths = [UNIT/n for n in ['h417_ownership.hpp', 'h417_ownership_driver.cpp',
                                 'h417_ownership_oracle.py', Path(__file__).name]] + [INPUT]
        for path in paths:
            dest = out/'inputs'/path.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        (out/'native-build.json').write_text(json.dumps(dict(command=command,
            compiler=compiler, compiler_sha256=sha(compiler), binary_sha256=sha(exe),
            stdout=p.stdout, stderr=p.stderr,
            inputs={str(p.relative_to(REPO)): sha(p) for p in paths}), indent=2)+'\n')
    return exe


def replay(exe, mode, commands, domain=7, short=False):
    stdin = '\n'.join(commands)+'\n'
    args = [str(exe), str(mode), str(domain), 'short' if short else 'normal']
    p = subprocess.run(args, input=stdin, capture_output=True, text=True, timeout=60)
    if destination := os.environ.get('EXP05_H417_OWNERSHIP_EVIDENCE'):
        trace = dict(args=args[1:], stdin=stdin, stdout=p.stdout, stderr=p.stderr, returncode=p.returncode)
        key = hashlib.sha256(json.dumps([args[1:], stdin]).encode()).hexdigest()
        path = Path(destination)/f'trace-{key}.json'
        if path.exists():
            assert json.loads(path.read_text()) == trace
        else:
            path.write_text(json.dumps(trace, indent=2)+'\n')
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    assert len(rows) == len(commands)
    model = reference.Model(mode, domain, short)
    for n, (command, row) in enumerate(zip(commands, rows)):
        assert row == model.apply(command), (n, command, row)
    return rows


def consumer(mode):
    return 0 if mode == 0 else 1


def fill(core, token, base=0):
    return [f'write {core} {token} {n} {(n+base)&255}' for n in range(64)]


@pytest.mark.parametrize('mode', range(3))
def test_message_fifo_full_wrap_and_notifications_are_only_hints(native, mode):
    c = consumer(mode)
    commands = [f'push 0 {n} {n+1} {n+2} {n+3}' for n in range(5)]
    commands += [f'ack {c} 1', 'snapshot', f'pop {c}', 'push 0 5 6 7 8']
    commands += [f'pop {c}']*5 + [f'ack {c} 3']
    rows = replay(native, mode, commands)
    assert not rows[4]['ok'] and rows[6]['state'][6] == 4 and rows[6]['state'][5] == 0
    assert [r['message'][0] for cmd, r in zip(commands, rows) if cmd == f'pop {c}' and r['ok']] == [0, 1, 2, 3, 5]


@pytest.mark.parametrize('mode', range(3))
def test_two_blocks_publish_order_reverse_return_and_stale_token(native, mode):
    c = consumer(mode)
    commands = ['reserve 0', 'reserve 0', 'reserve 0']
    commands += fill(0, '7 1 1 0') + fill(0, '7 1 2 1', 70)
    commands += ['publish 0 7 1 2 1', 'publish 0 7 1 1 0', f'ack {c} 2', 'reserve 0',
                 f'take {c}', f'take {c}', 'reserve 0', f'read {c} 7 1 2 1 63',
                 f'release {c} 7 1 1 0', 'reserve 0', f'release {c} 7 1 2 1',
                 f'release {c} 7 1 1 0', f'read {c} 7 1 2 1 0',
                 'publish 0 7 1 1 0', 'write 0 7 1 1 0 0 88']
    rows = replay(native, mode, commands)
    taken = [r['token'][2:] for cmd, r in zip(commands, rows) if cmd == f'take {c}']
    assert taken == [[2, 1], [1, 0]]
    assert rows[-6]['token'] == [7, 1, 3, 0]
    assert not any(r['ok'] for r in rows[-4:])


@pytest.mark.parametrize('mode', range(3))
@pytest.mark.parametrize('mask', range(8))
def test_recovery_requires_all_quiescent_roles_and_closes_all_data_access(native, mode, mask):
    c = consumer(mode)
    commands = ['push 0 1 2 3 4', 'reserve 0'] + fill(0, '7 1 1 0')
    commands += ['publish 0 7 1 1 0', f'take {c}', 'reserve 0', 'recover 0',
                 f'attest 0 {mask}', 'restart 0', 'push 0 5 6 7 8',
                 f'pop {c}', f'read {c} 7 1 1 0 0', f'release {c} 7 1 1 0',
                 'write 0 7 1 2 1 0 8', 'publish 0 7 1 2 1', f'take {c}',
                 'attest 0 7', 'restart 0', 'reserve 0', f'release {c} 7 1 1 0',
                 'write 0 7 1 2 1 0 9']
    rows = replay(native, mode, commands)
    start = commands.index('restart 0')
    assert bool(rows[start]['ok']) == (mask == 7)
    if mask != 7:
        assert not any(r['ok'] for r in rows[start:start+8])
    assert rows[-3]['token'] == [7, 2, 3, 0]
    assert not rows[-2]['ok'] and not rows[-1]['ok']


@pytest.mark.parametrize('mode', range(3))
def test_busy_gate_rejects_without_changing_outputs_or_ownership(native, mode):
    c = consumer(mode)
    commands = ['reserve 0'] + fill(0, '7 1 1 0')
    commands += ['publish 0 7 1 1 0', 'gate 1', 'reserve 0', 'push 0 1 2 3 4',
                 f'take {c}', f'ack {c} 3', 'recover 0', 'attest 0 7', 'restart 0',
                 f'release {c} 7 1 1 0', 'gate 0', f'take {c}', f'read {c} 7 1 1 0 63']
    rows = replay(native, mode, commands)
    start = commands.index('gate 1')
    for r in rows[start+1:start+9]:
        assert not r['ok'] and {k:v for k,v in r.items() if k != 'ok'} == {k:v for k,v in rows[start].items() if k != 'ok'}
    assert rows[-1]['ok'] and rows[-1]['byte'] == 63


@pytest.mark.parametrize('mode', range(3))
def test_wrong_owner_domain_epoch_and_partial_publication(native, mode):
    c = consumer(mode)
    commands = ['reserve 0', 'publish 0 7 1 1 0', 'write 1 7 1 1 0 0 8',
                 'write 0 8 1 1 0 0 8', 'write 0 7 2 1 0 0 8',
                 'write 0 7 1 2 0 0 8', 'write 0 7 1 1 2 0 8',
                 'write 0 7 1 1 0 1 8', 'write 0 7 1 1 0 0 256']
    commands += fill(0, '7 1 1 0') + ['publish 0 7 1 1 0', f'take {c}',
                f'release {c^1} 7 1 1 0', f'read {c^1} 7 1 1 0 0',
                f'read {c} 7 1 1 0 64', f'release {c} 7 1 1 0']
    rows = replay(native, mode, commands)
    assert not any(r['ok'] for r in rows[1:9])
    assert not any(r['ok'] for r in rows[-4:-1]) and rows[-1]['ok']


@pytest.mark.parametrize('mode', range(3))
def test_no_counter_reuse_when_generation_exhausts(native, mode):
    commands = []
    for _ in range(255):
        commands += ['reserve 0', 'recover 0', 'attest 0 7', 'restart 0']
    # The final restart also exhausts the shortened epoch. Old tokens stay invalid.
    commands += ['reserve 0', 'write 0 7 1 1 0 0 1', 'restart 0']
    rows = replay(native, mode, commands, short=True)
    assert rows[-4]['state'][1:4] == [255, 255, 1]
    assert not any(r['ok'] for r in rows[-4:])


@pytest.mark.parametrize('mode', range(3))
def test_epoch_exhaustion_without_allocations(native, mode):
    commands = ['recover 0', 'attest 0 7', 'restart 0']*255
    commands += ['reserve 0', 'push 0 1 2 3 4', 'restart 0']
    rows = replay(native, mode, commands, short=True)
    assert rows[-4]['state'][1:4] == [255, 0, 1]
    assert not any(r['ok'] for r in rows[-4:])


@pytest.mark.parametrize('mode', range(3))
def test_generation_exhaustion_without_restarts(native, mode):
    c = consumer(mode)
    commands = []
    for g in range(1, 256):
        commands += ['reserve 0'] + fill(0, f'7 1 {g} 0')
        commands += [f'publish 0 7 1 {g} 0', f'take {c}', f'release {c} 7 1 {g} 0']
    commands += ['reserve 0', 'recover 0', 'attest 0 7', 'restart 0', 'reserve 0']
    rows = replay(native, mode, commands, short=True)
    assert not rows[-5]['ok'] and rows[-2]['ok'] and not rows[-1]['ok']
    assert rows[-1]['state'][1:3] == [2, 255]


def test_shared_access_still_tracks_the_borrowing_core(native):
    commands = ['reserve 1'] + fill(1, '7 1 1 0')
    commands += ['publish 0 7 1 1 0', 'publish 1 7 1 1 0', 'take 0',
                 'read 1 7 1 1 0 0', 'release 1 7 1 1 0',
                 'read 0 7 1 1 0 63', 'release 0 7 1 1 0']
    rows = replay(native, 2, commands)
    assert not rows[-7]['ok'] and not rows[-4]['ok'] and not rows[-3]['ok']
    assert rows[-2]['byte'] == 63 and rows[-1]['ok']


@pytest.mark.parametrize('mode', range(3))
@pytest.mark.parametrize('phase', ['writing-empty', 'writing-partial', 'writing-full', 'ready', 'reading'])
def test_recovery_from_each_lease_phase_does_not_claim_zeroed_or_retained_data(native, mode, phase):
    c = consumer(mode)
    commands = ['reserve 0']
    count = 0 if phase == 'writing-empty' else 17 if phase == 'writing-partial' else 64
    commands += fill(0, '7 1 1 0', 100)[:count]
    if phase in ('ready', 'reading'):
        commands += ['publish 0 7 1 1 0']
    if phase == 'reading':
        commands += [f'take {c}']
    commands += ['push 0 11 22 33 44', 'recover 0', 'attest 0 1', 'restart 0',
                 'attest 0 2', 'restart 0', 'attest 0 4', 'restart 0',
                 f'pop {c}', f'take {c}', 'reserve 0', f'read {c} 7 1 1 0 0']
    commands += fill(0, '7 2 2 0', 200)
    commands += ['publish 0 7 2 2 0', f'take {c}', f'read {c} 7 2 2 0 0', f'read {c} 7 2 2 0 63']
    rows = replay(native, mode, commands)
    restart = [r for cmd, r in zip(commands, rows) if cmd == 'restart 0']
    assert [r['ok'] for r in restart] == [0, 0, 1]
    assert not next(r['ok'] for cmd, r in zip(commands, rows) if cmd == f'pop {c}')
    assert rows[-2]['byte'] == 200 and rows[-1]['byte'] == 7


@pytest.mark.parametrize('mode', range(3))
def test_endpoint_policy_and_recovery_coordinator_authority(native, mode):
    rows = replay(native, mode, ['push 1 1 2 3 4', 'reserve 1', 'recover 1',
                                'recover 2', 'recover 0', 'attest 1 7', 'attest 0 7',
                                'restart 1', 'restart 0'])
    assert bool(rows[0]['ok']) == bool(rows[1]['ok']) == (mode == 2)
    assert not rows[2]['ok'] and not rows[3]['ok'] and rows[4]['ok']
    assert not rows[5]['ok'] and not rows[7]['ok'] and rows[8]['ok']


@pytest.mark.parametrize('mode', range(3))
def test_invalid_domain_never_admits(native, mode):
    rows = replay(native, mode, ['reserve 0', 'push 0 1 2 3 4', 'recover 0'], domain=0)
    assert not any(r['ok'] for r in rows)


@pytest.mark.parametrize('mode', range(3))
@pytest.mark.parametrize('seed', CONFIG['trace_seeds'])
def test_generated_serial_ownership_and_recovery(native, mode, seed):
    rng = random.Random(0x41700+seed)
    model = reference.Model(mode)
    commands = []
    def emit(command):
        commands.append(command)
        return model.apply(command)
    for n in range(CONFIG['trace_operations']):
        # Reopen and complete real transfers periodically; pure random noise
        # otherwise spends most of its time correctly rejected during recovery.
        if n % 75 == 0:
            emit('gate 0')
            if not model.recovering:
                emit('recover 0')
            emit('attest 0 7'); emit('restart 0')
            p = rng.randrange(2) if mode == 2 else 0
            c = rng.randrange(2) if mode == 2 else consumer(mode)
            emit(f'push {p} {n} 2 3 4'); emit(f'pop {c}')
            row = emit(f'reserve {p}')
            assert row['ok']
            token = ' '.join(map(str, row['token']))
            for command in fill(p, token, n): emit(command)
            emit(f'publish {p} {token}'); emit(f'take {c}')
            emit(f'read {c} {token} 63'); emit(f'release {c} {token}')
        core = rng.randrange(3)
        op = rng.randrange(14)
        if op == 0: emit(f'push {core} {rng.getrandbits(32)} 2 3 4')
        elif op == 1: emit(f'pop {core}')
        elif op == 2: emit(f'reserve {core}')
        elif op == 3: emit(f'take {core}')
        elif op == 4: emit(f'ack {core} {rng.randrange(5)}')
        elif op == 5: emit(f'recover {core}')
        elif op == 6: emit(f'attest {core} {rng.randrange(9)}')
        elif op == 7: emit(f'restart {core}')
        elif op == 8: emit(f'gate {rng.randrange(2)}')
        else:
            i = rng.randrange(2); b = model.blocks[i]
            token = f'7 {model.epoch} {b["generation"]} {i}'
            if rng.randrange(4) == 0: token = '7 1 1 0'
            if op == 9: emit(f'write {core} {token} {len(b["data"])} {rng.randrange(256)}')
            elif op == 10: emit(f'publish {core} {token}')
            elif op == 11: emit(f'release {core} {token}')
            elif op == 12: emit(f'read {core} {token} {rng.randrange(65)}')
            else: emit('snapshot')
    emit('gate 0')
    replay(native, mode, commands)
