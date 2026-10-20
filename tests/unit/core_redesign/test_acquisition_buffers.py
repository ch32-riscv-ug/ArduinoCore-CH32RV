"""Two-block acquisition ownership against an independent serial list model."""
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
INPUT = REPO / 'planning/core-redesign/acquisition-inputs.json'
CONFIG = json.loads(INPUT.read_text())
spec = importlib.util.spec_from_file_location('acquisition_reference', UNIT/'acquisition_oracle.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


@pytest.fixture(scope='module')
def acquisition_native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    exe = tmp_path_factory.mktemp('acquisition')/'control'
    flags = ['-std=c++17', '-O1', '-g1', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    p = subprocess.run([compiler, *flags, str(UNIT/'acquisition_driver.cpp'), '-o', str(exe)], capture_output=True, text=True, timeout=180)
    assert p.returncode == 0, p.stderr
    if destination := os.environ.get('EXP02_ACQUISITION_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(exe, out/'native-control')
        sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
        paths = [UNIT/name for name in ['acquisition_buffers.hpp', 'acquisition_driver.cpp', 'acquisition_oracle.py', Path(__file__).name]] + [INPUT]
        (out/'native-build.json').write_text(json.dumps(dict(compiler=compiler, compiler_sha256=sha(compiler),
            flags=flags, stdout=p.stdout, stderr=p.stderr, binary_sha256=sha(exe),
            inputs={str(p.relative_to(REPO)): sha(p) for p in paths}), indent=2)+'\n')
    return exe


def replay(exe, commands, samples=64, short=False):
    p = subprocess.run([str(exe), str(samples), 'short' if short else 'normal'],
                       input='\n'.join(commands)+'\n', capture_output=True, text=True, timeout=60)
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    assert len(rows) == len(commands)
    model = reference.Model(samples, short)
    for n, (command, row) in enumerate(zip(commands, rows)):
        expected = model.apply(command)
        assert row == expected, (n, command, row, expected)
    return rows


def fill(g, i, samples, base=100):
    return [f'write {g} {i} {j} {(base+j) & 65535}' for j in range(samples)]


@pytest.mark.parametrize('samples', [8, 64, 128])
def test_completion_order_reverse_release_and_no_overwrite(acquisition_native, samples):
    commands = ['reserve 1', 'open', 'open', 'reserve 10', 'reserve 20', 'reserve 30']
    commands += fill(1, 0, samples) + fill(2, 1, samples, 400)
    commands += [f'finish 2 1 0 {samples} 1', f'finish 1 0 0 {samples} 1', 'notice',
                 'ack 2 1', 'notice', 'reserve 30', 'take', 'take', 'write 1 0 0 999',
                 'reserve 30', 'release 1 0', 'reserve 30', 'release 1 0', 'release 2 1',
                 'write 1 0 0 999', 'sample 2 1 0', 'sample 3 0 0']
    rows = replay(acquisition_native, commands, samples)
    assert rows[5]['status'] == 2
    taken = [row['completion'][:3] for cmd, row in zip(commands, rows) if cmd == 'take']
    assert taken == [[2, 1, 20], [1, 0, 10]]
    assert rows[-6]['token'] == [3, 0]
    assert rows[-5]['status'] == 0


@pytest.mark.parametrize('result', [0, 1, 2])
@pytest.mark.parametrize('stop', ['cancel', 'end'])
def test_cancel_is_not_stop_or_collection(acquisition_native, result, stop):
    commands = ['open', 'reserve 10'] + fill(1, 0, 64)
    commands += ['cancel 1 0' if stop == 'cancel' else 'end', 'release 1 0',
                 f'finish 1 0 {result} 64 0', 'open', 'take',
                 f'finish 1 0 {result} 64 1', f'finish 1 0 {result} 64 1',
                 'release 1 0', 'open', 'notice', 'take', 'sample 1 0 63',
                 'open', 'release 1 0', 'end', 'open', 'reserve 20', 'release 1 0']
    rows = replay(acquisition_native, commands)
    terminal = next(row for cmd, row in zip(commands, rows) if cmd == f'finish 1 0 {result} 64 1')
    assert terminal['status'] == 1 and terminal['blocks'][0][4] == 2
    assert rows[-2]['token'][0] == 2 and rows[-1]['status'] == 0


@pytest.mark.parametrize('count', [0, 1, 63, 64])
@pytest.mark.parametrize('result', [1, 2])
def test_partial_prefix_and_stale_data_are_not_exposed(acquisition_native, count, result):
    commands = ['open', 'reserve 1'] + fill(1, 0, 64)
    commands += ['finish 1 0 0 64 1', 'take', 'release 1 0', 'reserve 2', 'reserve 3']
    commands += fill(3, 0, count, 65000)
    commands += ['cancel 3 0', f'finish 3 0 {result} {count+1} 1',
                 f'finish 3 0 {result} {count} 1', 'take', f'sample 3 0 {count}',
                 'sample 3 0 0', 'release 3 0']
    rows = replay(acquisition_native, commands)
    assert rows[-4]['status'] == 1 and rows[-3]['status'] == 0
    if count: assert rows[-2]['sample'] == 65000


def test_notice_ack_does_not_collect_and_stale_ack_cannot_clear_reuse(acquisition_native):
    commands = ['open', 'reserve 1', 'finish 1 0 1 0 1', 'ack 1 0', 'notice',
                'release 1 0', 'take', 'release 1 0', 'reserve 2', 'reserve 3',
                'finish 3 0 1 0 1', 'ack 1 0', 'notice', 'end', 'notice', 'take',
                'release 3 0', 'open', 'finish 2 1 2 0 1', 'take', 'release 2 1', 'open']
    rows = replay(acquisition_native, commands)
    assert rows[4]['status'] == 0 and rows[5]['status'] == 0 and rows[6]['status'] == 1
    assert rows[11]['status'] == 0 and rows[12]['token'] == [3, 0]
    assert rows[17]['status'] == 0 and rows[-1]['status'] == 1


def test_invalid_transitions_preserve_outputs(acquisition_native):
    commands = ['take', 'notice', 'sample 0 0 0', 'release 0 0', 'open', 'reserve 1',
                'write 1 0 1 99', 'finish 1 0 0 0 1', 'finish 1 0 2 0 1',
                'finish 1 0 3 0 1', 'write 1 0 64 99', 'ack 1 0', 'release 1 0',
                'write 1 2 0 99', 'finish 2 0 1 0 1', 'finish 1 0 1 0 1',
                'write 1 0 0 99', 'cancel 1 0', 'take', 'sample 1 0 0', 'sample 1 0 65535']
    rows = replay(acquisition_native, commands)
    assert rows[-1]['sample'] == 54321 and rows[-1]['completion'] == [1, 0, 1, 0, 1]


def test_generation_never_recycles_across_reopen(acquisition_native):
    commands = []
    for generation in range(1, 256):
        index = (generation-1) % 2
        commands += ['open', 'reserve 4294967295', f'finish {generation} {index} 1 0 1',
                     'take', f'release {generation} {index}', 'end']
    commands += ['open', 'reserve 0', 'release 1 0', 'notice', 'take']
    rows = replay(acquisition_native, commands, short=True)
    assert rows[-4]['status'] == 3 and rows[-4]['token'][0] == 255


@pytest.mark.parametrize('samples', [8, 64, 128])
@pytest.mark.parametrize('seed', CONFIG['trace_seeds'])
def test_generated_ownership_trace(acquisition_native, samples, seed):
    rng = random.Random(0xadc0+seed)
    model = reference.Model(samples)
    commands = []
    def emit(command):
        commands.append(command)
        model.apply(command)
    emit('open')
    for n in range(CONFIG['trace_operations']):
        i = rng.randrange(2)
        b = model.blocks[i]
        g = b['g'] if rng.randrange(5) else max(0, b['g']-1)
        choice = rng.randrange(14)
        command = ['open', 'end', f'reserve {n}', f'write {g} {i} {len(b["values"])} {rng.randrange(65536)}',
                   f'cancel {g} {i}', f'finish {g} {i} 1 {len(b["values"])} 1',
                   f'finish {g} {i} 2 {len(b["values"])} 1', 'notice', f'ack {g} {i}',
                   'take', f'sample {g} {i} {rng.randrange(samples+1)}', f'release {g} {i}',
                   f'finish {g} {i} 0 {samples} 1', f'finish {g} {i} 1 0 0'][choice]
        emit(command)
    replay(acquisition_native, commands, samples)


@pytest.mark.parametrize('stall', CONFIG['workload_stall_ticks'])
@pytest.mark.parametrize('mode', CONFIG['source_modes'])
def test_stalled_consumer_workload(acquisition_native, stall, mode):
    """Arrivals are abstract ticks, not ADC Hz. Failed admission != known loss."""
    model = reference.Model(64)
    commands = []
    accepted = lost = deferred = 0
    held = []
    def emit(command):
        commands.append(command)
        return model.apply(command)
    emit('open')
    for tick in range(CONFIG['workload_arrivals']):
        result = emit(f'reserve {tick}')
        if result['status'] == 0:
            accepted += 1
            g, i = result['token']
            for command in fill(g, i, 64, tick*100): emit(command)
            emit(f'finish {g} {i} 0 64 1')
        elif mode == 'unavoidable-arrival': lost += 64
        else: deferred += 1
        # Delivery sink is deliberately unavailable. Sticky completion survives.
        emit('notice')
        if tick % (stall+1) == 0:
            while model.ready:
                row = emit('take')
                held.append(row['completion'][:2])
            for g, i in reversed(held):
                for j in range(64): emit(f'sample {g} {i} {j}')
                emit(f'release {g} {i}')
            held.clear()
    emit('end')
    while model.ready:
        row = emit('take')
        g, i = row['completion'][:2]
        emit(f'release {g} {i}')
    emit('open')
    rows = replay(acquisition_native, commands)
    assert rows[-1]['status'] == 1
    assert accepted + deferred + lost//64 == CONFIG['workload_arrivals']
    if stall == 0: assert accepted == CONFIG['workload_arrivals']
    else: assert accepted < CONFIG['workload_arrivals']
    if mode == 'software-rearm': assert lost == 0
