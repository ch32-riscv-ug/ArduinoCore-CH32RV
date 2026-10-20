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
LEASE_CONFIG = json.loads((REPO/'planning/core-redesign/sleep-lease-fault-inputs.json').read_text())
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
                 'io_lifecycle.hpp', 'logical_time.hpp', 'deadline_timer.hpp']] + [REPO/'planning/core-redesign/sleep-features-inputs.json', REPO/'planning/core-redesign/sleep-lease-fault-inputs.json']
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


@pytest.mark.parametrize('variant', LEASE_CONFIG['variants'])
@pytest.mark.parametrize('timed', LEASE_CONFIG['timer_modes'])
@pytest.mark.parametrize('mode', range(len(LEASE_CONFIG['failure_modes'])), ids=LEASE_CONFIG['failure_modes'])
@pytest.mark.parametrize('topology', LEASE_CONFIG['topologies'], ids=lambda t: f"fault{t['fault_side']}-lease{t['lease_side']}-both{int(t['both'])}-reverse{int(t['reverse'])}")
@pytest.mark.parametrize('outcome', LEASE_CONFIG['outcomes'])
def test_live_lease_survives_fault_retry_terminal_and_stale_completion(fault_native, variant, timed, mode, topology, outcome):
    t = topology
    command = [str(fault_native), 'lease', str(variant), str(int(timed)), str(mode),
               str(t['fault_side']), str(t['lease_side']), str(int(t['both'])), str(int(t['reverse'])), str(outcome)]
    p = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert p.returncode == 0, p.stderr
    assert not p.stderr
    rows = [json.loads(line) for line in p.stdout.splitlines()]
    by_label = {row['label']: row for row in rows}
    for row in rows:
        assert row['attempt'] == 1 and row['clock_phase'] == 1
        for port in row['ports']:
            assert port['active'] == 1-port['available']
            assert not port['terminal'] or port['active']
        if row['phase'] == 6:
            assert row['frozen'] and all(not port['admitting'] for port in row['ports'])
    initial = by_label['injected-with-live-lease']
    assert initial['phase'] == 6 and initial['input'] == int(mode != 0)
    for i, port in enumerate(initial['ports']):
        assert port['active'] == int(t['both'] or i == t['lease_side'])
        if port['active']: assert port['cancelled'] and not port['available'] and port['ticket'] == 1
    invalid = by_label['invalid-stop-and-stale-take']
    assert invalid['ports'] == by_label['retry-before-stop']['ports']
    first = int(t['reverse']) if t['both'] else t['lease_side']
    for row in rows:
        if row['label'].endswith('terminal-not-collected'):
            assert row['phase'] == 6
            assert any(port['terminal'] and port['active'] and not port['available'] for port in row['ports'])
    repaired = by_label['repaired']
    assert repaired['phase'] == 1 and not repaired['frozen'] and repaired['input'] == int(mode != 0)
    for i, port in enumerate(repaired['ports']):
        assert not port['active'] and port['available'] and port['admitting']
        if t['both'] or i == t['lease_side']:
            assert port['result'] == outcome and port['count'] == (4 if outcome == 1 else 2)
            assert port['read'] == 77+53*i
    if t['both']:
        held = by_label['one-lease-still-held']
        assert held['phase'] == 6 and held['ports'][first]['available'] and held['ports'][first ^ 1]['active']
    stale = by_label['old-result-cannot-release-new-lease']
    assert stale['ports'][first]['ticket'] == 2 and stale['ports'][first]['active'] and not stale['ports'][first]['available']
    final = by_label['final-collected']
    assert final['phase'] == 1 and not final['input'] and all(p['available'] and not p['active'] for p in final['ports'])
