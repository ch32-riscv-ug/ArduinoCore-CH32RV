"""Feature projection plus the complete existing ownership contract battery."""
import hashlib
import importlib.util
import inspect
import itertools
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
INPUT = REPO/'planning/core-redesign/h417-features-inputs.json'
CONFIG = json.loads(INPUT.read_text())
spec = importlib.util.spec_from_file_location('h417_baseline_battery', UNIT/'test_h417_ownership.py')
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)


class Projection:
    def __init__(self, mode, features, domain=7, short=False):
        self.base = baseline.reference.Model(mode, domain, short)
        self.features = features

    def apply(self, command):
        op = command.split()[0]
        disabled = ((op in ('push', 'pop') and not self.features & 1) or
                    (op in ('reserve', 'write', 'publish', 'take', 'read', 'release') and not self.features & 2))
        row = self.base.apply('snapshot' if disabled else command)
        if disabled:
            row['ok'] = 0
        if not self.features & 2:
            row['blocks'] = []
        return row


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    exe = tmp_path_factory.mktemp('h417-features')/'control'
    flags = ['-std=c++17', '-O0', '-g1', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    command = [compiler, *flags, str(UNIT/'h417_features_driver.cpp'), '-o', str(exe)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert p.returncode == 0, p.stderr
    if destination := os.environ.get('EXP05_H417_FEATURES_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(exe, out/'native-control')
        paths = [UNIT/n for n in ['h417_features.hpp','h417_features_driver.cpp',Path(__file__).name,
                                 'test_h417_ownership.py','h417_ownership_oracle.py']]
        paths += [INPUT, baseline.INPUT]
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        for path in paths:
            dest = out/'inputs'/path.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
        (out/'native-build.json').write_text(json.dumps(dict(command=command,
            compiler=compiler,compiler_sha256=sha(compiler),binary_sha256=sha(exe),
            stdout=p.stdout,stderr=p.stderr,
            inputs={str(p.relative_to(REPO)):sha(p) for p in paths}),indent=2)+'\n')
    return exe


def replay(exe, mode, commands, domain=7, short=False, features=3):
    args = [str(exe),str(mode),str(domain),'short' if short else 'normal',str(features)]
    stdin = '\n'.join(commands)+'\n'
    p = subprocess.run(args,input=stdin,capture_output=True,text=True,timeout=60)
    if destination := os.environ.get('EXP05_H417_FEATURES_EVIDENCE'):
        key = hashlib.sha256(json.dumps([args[1:],stdin]).encode()).hexdigest()
        path = Path(destination)/f'trace-{key}.json'
        record = dict(args=args[1:],stdin=stdin,stdout=p.stdout,stderr=p.stderr,returncode=p.returncode)
        if path.exists(): assert json.loads(path.read_text()) == record
        else: path.write_text(json.dumps(record,indent=2)+'\n')
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    assert len(rows)==len(commands)
    model = Projection(mode,features,domain,short)
    for n,(command,row) in enumerate(zip(commands,rows)):
        assert row == model.apply(command),(n,command,row)
    return rows


# Reuse all 79 existing scenarios and their literal assertions without editing
# the frozen test module or maintaining a second copy of those scenarios.
baseline.replay = replay
values = dict(mode=range(3),mask=range(8),seed=baseline.CONFIG['trace_seeds'],
              phase=['writing-empty','writing-partial','writing-full','ready','reading'])
LEGACY = []
for name in sorted(dir(baseline)):
    if not name.startswith('test_'): continue
    function = getattr(baseline,name)
    names = list(inspect.signature(function).parameters)[1:]
    for args in itertools.product(*(values[n] for n in names)):
        LEGACY.append(pytest.param(function,args,id=name+'-'+','.join(map(str,args))))
assert len(LEGACY)==79


@pytest.mark.parametrize('function,args',LEGACY)
def test_full_feature_baseline_contract(native,function,args):
    function(native,*args)


@pytest.mark.parametrize('mode',range(3))
@pytest.mark.parametrize('features',range(3))
def test_disabled_calls_and_enabled_complete_transfers(native,mode,features):
    c = baseline.consumer(mode)
    commands = ['push 0 11 22 33 44','reserve 0'] + baseline.fill(0,'7 1 1 0')
    commands += ['publish 0 7 1 1 0',f'ack {c} 3',f'pop {c}',f'take {c}',
                 f'read {c} 7 1 1 0 63',f'release {c} 7 1 1 0',
                 'gate 1','push 0 9 9 9 9','reserve 0','recover 0','gate 0']
    rows = replay(native,mode,commands,features=features)
    assert bool(rows[0]['ok']) == bool(features & 1)
    assert bool(rows[1]['ok']) == bool(features & 2)
    if features & 1: assert next(r['message'] for cmd,r in zip(commands,rows) if cmd==f'pop {c}') == [11,22,33,44]
    if features & 2: assert next(r['byte'] for cmd,r in zip(commands,rows) if cmd==f'read {c} 7 1 1 0 63') == 63


@pytest.mark.parametrize('mode',range(3))
@pytest.mark.parametrize('features',range(3))
@pytest.mark.parametrize('mask',range(8))
def test_removing_storage_does_not_relax_recovery_attestations(native,mode,features,mask):
    commands = ['push 0 11 22 33 44','reserve 0','recover 0',f'attest 0 {mask}','restart 0',
                'push 0 55 66 77 88','reserve 0','attest 0 7','restart 0','reserve 0']
    rows = replay(native,mode,commands,features=features)
    assert bool(rows[4]['ok']) == (mask==7)
    assert rows[-1]['state'][1]==2


@pytest.mark.parametrize('mode',range(3))
@pytest.mark.parametrize('features',range(4))
@pytest.mark.parametrize('seed',CONFIG['seeds'])
def test_generated_projection_and_reuse(native,mode,features,seed):
    rng = random.Random(0x417f+seed)
    model = Projection(mode,features)
    commands = []
    def emit(command):
        commands.append(command)
        return model.apply(command)
    for n in range(CONFIG['operations']):
        if n % 50 == 0:
            emit('gate 0')
            if not model.base.recovering: emit('recover 0')
            emit('attest 0 7'); emit('restart 0')
            c = baseline.consumer(mode)
            emit(f'push 0 {n} 1 2 3'); emit(f'pop {c}')
            row = emit('reserve 0')
            if row['ok']:
                token = ' '.join(map(str,row['token']))
                for cmd in baseline.fill(0,token,n):emit(cmd)
                emit(f'publish 0 {token}');emit(f'take {c}')
                emit(f'read {c} {token} 63');emit(f'release {c} {token}')
        core = rng.randrange(3); i = rng.randrange(2); b = model.base.blocks[i]
        token = f'7 {model.base.epoch} {b["generation"]} {i}'
        operations = [f'push {core} {rng.getrandbits(32)} 1 2 3',f'pop {core}',f'reserve {core}',
                      f'write {core} {token} {len(b["data"])} {rng.randrange(256)}',
                      f'publish {core} {token}',f'take {core}',f'read {core} {token} {rng.randrange(65)}',
                      f'release {core} {token}',f'ack {core} {rng.randrange(5)}',f'recover {core}',
                      f'attest {core} {rng.randrange(9)}',f'restart {core}',f'gate {rng.randrange(2)}']
        emit(rng.choice(operations))
    replay(native,mode,commands,features=features)
