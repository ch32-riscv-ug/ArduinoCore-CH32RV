"""Post-action failures and source loss; this is not a real driver fault test."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
CONFIG = json.loads((REPO/'planning/core-redesign/sleep-features-inputs.json').read_text())
CASES = [(kind, reply) for kind in range(6) for reply in CONFIG['fault_replies']] + [(6, 2)]


@pytest.fixture(scope='module')
def fault_native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    exe = tmp_path_factory.mktemp('sleep-faults')/'control'
    flags = ['-std=c++17', '-O0', '-g1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
             '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    command = [compiler, *flags, str(UNIT/'sleep_fault_driver.cpp'), '-o', str(exe)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert p.returncode == 0, p.stderr
    if destination := os.environ.get('EXP06_SLEEP_FAULT_EVIDENCE'):
        out = Path(destination)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(exe, out/'native-control')
        sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
        paths = [UNIT/name for name in ['sleep_fault_driver.cpp', 'test_sleep_faults.py', 'sleep_coordinator.hpp',
                 'io_lifecycle.hpp', 'logical_time.hpp', 'deadline_timer.hpp']] + [REPO/'planning/core-redesign/sleep-features-inputs.json']
        for source in paths:
            dest = out/'inputs'/source.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
        (out/'native-build.json').write_text(json.dumps(dict(command=command, compiler_sha256=sha(compiler),
            binary_sha256=sha(exe), stdout=p.stdout, stderr=p.stderr,
            inputs={str(p.relative_to(REPO)): sha(p) for p in paths}), indent=2)+'\n')
    return exe


@pytest.mark.parametrize('variant', CONFIG['variants'])
@pytest.mark.parametrize('timed', CONFIG['timer_modes'])
@pytest.mark.parametrize('side', CONFIG['fault_sides'])
@pytest.mark.parametrize('kind,reply', CASES)
def test_stage_failure_closes_admission_until_repair(fault_native, variant, timed, side, kind, reply):
    p = subprocess.run([str(fault_native), str(variant), str(int(timed)), str(kind), str(side), str(reply)],
                       capture_output=True, text=True, timeout=30)
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    injected = rows[0]
    expected = 2 if kind in (0, 1) and reply == 0 else (5 if kind == 3 and reply == 0 else 6)
    assert injected['phase'] == expected
    assert injected['frozen'] and all(not port[1] for port in injected['ports'])
    assert all(sum(port[4]) < 20 for row in rows for port in row['ports'])
    if kind in (2, 6): assert not injected['clock_ready'] and injected['clock_phase'] == 2
    if kind == 5: assert injected['input'] and rows[-1]['input']
    if kind == 6:
        assert rows[1]['label'] == 'retry-with-paused-clock' and rows[1]['phase'] == 5
        assert not rows[1]['clock_ready'] and all(not port[1] for port in rows[1]['ports'])
    assert rows[-1]['phase'] == 1 and rows[-1]['clock_ready'] and not rows[-1]['frozen']
