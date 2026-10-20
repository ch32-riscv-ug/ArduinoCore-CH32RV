"""Two real host threads; default ASan/UBSan, explicit TSan rerun supported."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
INPUT = REPO/'planning/core-redesign/h417-thread-inputs.json'
CONFIG = json.loads(INPUT.read_text())


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    sanitizer = os.environ.get('EXP05_H417_THREAD_SANITIZER', 'address,undefined')
    assert sanitizer in CONFIG['sanitizers']
    exe = tmp_path_factory.mktemp('h417-threads')/'control'
    flags = ['-std=c++17', '-O1', '-g1', '-pthread', '-Wall', '-Wextra', '-Werror',
             f'-fsanitize={sanitizer}', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    command = [compiler, *flags, str(UNIT/'h417_thread_driver.cpp'), '-o', str(exe)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert p.returncode == 0, p.stderr
    if destination := os.environ.get('EXP05_H417_THREAD_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(exe, out/'native-control')
        paths = [UNIT/n for n in ['h417_ownership.hpp', 'h417_thread_driver.cpp', Path(__file__).name]] + [INPUT]
        for path in paths:
            dest = out/'inputs'/path.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        (out/'native-build.json').write_text(json.dumps(dict(command=command,
            compiler=compiler, compiler_sha256=sha(compiler), binary_sha256=sha(exe), sanitizer=sanitizer,
            stdout=p.stdout, stderr=p.stderr,
            inputs={str(p.relative_to(REPO)): sha(p) for p in paths}), indent=2)+'\n')
    return exe


def run(exe, gate, mode, seed=0):
    args = [str(exe), gate, mode, str(seed), str(CONFIG['messages_per_producer']), str(CONFIG['blocks_per_producer'])]
    p = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if destination := os.environ.get('EXP05_H417_THREAD_EVIDENCE'):
        path = Path(destination)/f'{gate}-{mode}-{seed}.json'
        path.write_text(json.dumps(dict(args=args[1:], returncode=p.returncode,
                                       stdout=p.stdout, stderr=p.stderr), indent=2)+'\n')
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    return json.loads(p.stdout)


@pytest.mark.parametrize('gate', CONFIG['gates'])
@pytest.mark.parametrize('mode', CONFIG['modes'])
@pytest.mark.parametrize('seed', CONFIG['seeds'])
def test_two_thread_message_and_block_integrity_fifo_and_ownership(native, gate, mode, seed):
    row = run(native, gate, mode, seed)
    producers = 2 if mode == 'shared' else 1
    assert row['messages'] == producers*CONFIG['messages_per_producer']
    assert row['blocks'] == producers*CONFIG['blocks_per_producer']
    assert row['attempts'] >= row['messages']*2 + row['blocks']*(CONFIG['block_bytes']*2+4)
    assert 0 <= row['busy'] < row['attempts']


@pytest.mark.parametrize('gate', CONFIG['gates'])
def test_gate_held_by_other_thread_rejects_all_operations_without_mutating_outputs(native, gate):
    assert run(native, gate, 'busy') == dict(rejected=12, busy=12)


@pytest.mark.parametrize('gate', CONFIG['gates'])
@pytest.mark.parametrize('mode', CONFIG['modes'])
def test_parked_workers_quiescent_handshake_and_old_epoch_rejection(native, gate, mode):
    assert run(native, gate, f'recovery-{mode}') == dict(epoch=2, old_calls_rejected=5, fresh_bytes=64)
