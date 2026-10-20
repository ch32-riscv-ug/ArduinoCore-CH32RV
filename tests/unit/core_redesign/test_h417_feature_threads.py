"""Apply the unchanged host thread workload to each new feature type.

Derived translation units change only the include, namespace, type arguments,
disabled workload counts, and the post-unlock probe for absent block storage.
The exact generated source is archived so that
the original driver remains a frozen input to earlier results.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
INPUT = REPO/'planning/core-redesign/h417-feature-thread-inputs.json'
CONFIG = json.loads(INPUT.read_text())


def translation_unit():
    source = (REPO/CONFIG['source_workload']).read_text()
    replacements = [
        ('#include "h417_ownership.hpp"', '#include "h417_features.hpp"', 1),
        ('using namespace h417_experiment;', 'using namespace h417_feature_experiment;', 1),
        ('Fabric<Policy, Gate>', 'Fabric<Policy, Gate, (FEATURES & 1) != 0, (FEATURES & 2) != 0>', 2),
        ('Fabric<Owners::Fixed, Gate>', 'Fabric<Owners::Fixed, Gate, (FEATURES & 1) != 0, (FEATURES & 2) != 0>', 1),
        ('Token t{}; assert(fabric.reserve(0, t));',
         'Token t{}; assert(fabric.reserve(0, t) == ((FEATURES & 2) != 0));\n'
         '    assert(fabric.ack(1, 3));', 1),
    ]
    for old, new, count in replacements:
        assert source.count(old) == count, old
        source = source.replace(old, new)
    anchor = '    if (std::string(argv[1]) == "mutex")'
    assert source.count(anchor) == 1
    source = source.replace(anchor, '    if (!(FEATURES & 1)) messages = 0;\n'
                            '    if (!(FEATURES & 2)) blocks = 0;\n'+anchor)
    return source


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    sanitizer = os.environ.get('EXP05_H417_FEATURE_THREAD_SANITIZER', 'address,undefined')
    assert sanitizer in CONFIG['sanitizers']
    directory = tmp_path_factory.mktemp('h417-feature-threads')
    source = directory/'derived.cpp'
    source.write_text(translation_unit())
    flags = ['-std=c++17', '-O1', '-g1', '-pthread', '-Wall', '-Wextra', '-Werror',
             f'-fsanitize={sanitizer}', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    out = None
    if destination := os.environ.get('EXP05_H417_FEATURE_THREAD_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(source, out/'derived.cpp')
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    paths = [UNIT/'h417_features.hpp', REPO/CONFIG['source_workload'], Path(__file__), INPUT]
    report = dict(compiler=compiler,compiler_sha256=sha(compiler),sanitizer=sanitizer,
                  derived_sha256=sha(source),inputs={str(p.relative_to(REPO)):sha(p) for p in paths}, builds=[])
    if out:
        for path in paths:
            dest = out/'inputs'/path.relative_to(REPO)
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,dest)
    binaries = {}
    for features in CONFIG['features']:
        exe = directory/f'control-{features}'
        command = [compiler,*flags,f'-DFEATURES={features}','-I',str(UNIT),str(source),'-o',str(exe)]
        p = subprocess.run(command,capture_output=True,text=True,timeout=180)
        item = dict(features=features,command=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        report['builds'].append(item)
        if out: (out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
        assert p.returncode == 0,p.stderr
        item['binary_sha256'] = sha(exe)
        if out:
            shutil.copy2(exe,out/exe.name)
            (out/'native-build.json').write_text(json.dumps(report,indent=2)+'\n')
        binaries[features] = exe
    return binaries


def run(binaries,features,gate,mode,seed=0):
    args = [str(binaries[features]),gate,mode,str(seed),str(CONFIG['messages_per_producer']),str(CONFIG['blocks_per_producer'])]
    p = subprocess.run(args,capture_output=True,text=True,timeout=60)
    if destination := os.environ.get('EXP05_H417_FEATURE_THREAD_EVIDENCE'):
        path = Path(destination)/f'f{features}-{gate}-{mode}-{seed}.json'
        path.write_text(json.dumps(dict(features=features,args=args[1:],returncode=p.returncode,
                                       stdout=p.stdout,stderr=p.stderr),indent=2)+'\n')
    assert p.returncode == 0,p.stderr
    assert not p.stderr
    return json.loads(p.stdout)


@pytest.mark.parametrize('features',CONFIG['features'])
@pytest.mark.parametrize('gate',CONFIG['gates'])
@pytest.mark.parametrize('mode',CONFIG['modes'])
@pytest.mark.parametrize('seed',CONFIG['seeds'])
def test_enabled_payload_integrity_and_per_producer_fifo(native,features,gate,mode,seed):
    row = run(native,features,gate,mode,seed)
    producers = 2 if mode=='shared' else 1
    assert row['messages'] == (producers*CONFIG['messages_per_producer'] if features & 1 else 0)
    assert row['blocks'] == (producers*CONFIG['blocks_per_producer'] if features & 2 else 0)
    if features:
        assert row['attempts'] >= 2*row['messages']+(CONFIG['block_bytes']*2+4)*row['blocks']
        assert 0 <= row['busy'] < row['attempts']
    else:
        assert row['attempts']==row['busy']==0


@pytest.mark.parametrize('features',CONFIG['features'])
@pytest.mark.parametrize('gate',CONFIG['gates'])
def test_disabled_payload_methods_do_not_attempt_gate_and_outputs_survive_busy(native,features,gate):
    row = run(native,features,gate,'busy')
    assert row==dict(rejected=12,busy=CONFIG['busy_gate_calls'][str(features)])


@pytest.mark.parametrize('features',[2,3])
@pytest.mark.parametrize('gate',CONFIG['gates'])
@pytest.mark.parametrize('mode',CONFIG['modes'])
def test_block_recovery_still_requires_all_quiescent_roles(native,features,gate,mode):
    row = run(native,features,gate,f'recovery-{mode}')
    assert row==dict(epoch=2,old_calls_rejected=5,fresh_bytes=64)
