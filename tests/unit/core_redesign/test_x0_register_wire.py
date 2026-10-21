"""N02 direct collection through unchanged register engine and resource ledger."""
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
spec = importlib.util.spec_from_file_location('register_wire_reference',UNIT/'x0_wire_oracle.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


class Model(reference.Model):
    def apply(self,line):
        op,*args = line.split()
        args = list(map(int,args))
        current = self.current
        physical = current and current['kind'] == 0
        if op == 'time' and physical:
            due = current['due']
            current['due'] = 1 << 63
            result = super().apply(line)
            current['due'] = due
            return result
        if op == 'wire':
            if physical and current['outcome'] is None:
                current['success'] = not (args and args[0]) and not current.get('cancel_requested',False)
                current['due'] = self.now
                return super().apply(f'time {self.now}')
            return self.observe()
        if op == 'cancel' and physical:
            if current['outcome'] is None:
                current['cancel_requested'] = True
            return self.observe()
        if op == 'poll':
            if physical and current['outcome'] is None:
                if current.get('cancel_requested') and not current.get('dispatched'):
                    current['outcome'] = False
                elif current.get('cancel_requested'):
                    if current.get('stop_requested'):
                        current['outcome'] = False
                    else:
                        current['stop_requested'] = True
                else:
                    current['dispatched'] = True
            return self.observe()
        return super().apply(line)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    directory = tmp_path_factory.mktemp('x0-register')
    source = (UNIT/'i2c_master_driver.cpp').read_text()
    a = source.index('struct Fake {')
    b = source.index('template<bool Guarded',a)
    generated = directory/'i2c_handoff_fake.inc'
    generated.write_text(source[a:b])
    command = [compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror',
               '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
               '-I',str(directory),str(UNIT/'x0_register_wire_driver.cpp'),'-o',str(directory/'app')]
    result = subprocess.run(command,capture_output=True,text=True,timeout=240)
    if output := os.environ.get('EXP06_X0_REGISTER_EVIDENCE'):
        out = Path(output)
        out.mkdir(parents=True,exist_ok=False)
        shutil.copy2(generated,out/generated.name)
        paths = [*[UNIT/name for name in ['x0_register_wire.hpp','x0_wire.hpp','x0_features.hpp','x0_candidates.hpp',
                 'display_parts.hpp','r_application.hpp','r_workloads.hpp','data_parts.hpp','work_executor.hpp',
                 'event_executor.hpp','static_mailbox.hpp','i2c_managed_handoff.hpp','i2c_handoff.hpp','i2c_resources.hpp']],
                 UNIT/'x0_register_wire_driver.cpp',UNIT/'x0_wire_oracle.py',
                 UNIT/'app_oracle.py',UNIT/'oracle.py',UNIT/'i2c_master_driver.cpp',Path(__file__),
                 REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp',
                 REPO/'planning/core-redesign/x0-register-wire-inputs.json']
        for path in paths:
            target = out/'inputs'/path.relative_to(REPO)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,target)
        report = dict(command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr,
                      compiler_sha256=sha(compiler),fake_sha256=sha(generated),
                      compiler_version=subprocess.check_output([compiler,'--version'],text=True),
                      inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        if result.returncode == 0:
            shutil.copy2(directory/'app',out/'app')
            report['executable_sha256'] = sha(directory/'app')
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert result.returncode == 0,result.stderr
    return directory/'app'


def cycles(first,last,opportunity='step'):
    return [command for time in range(first,last,2) for command in [f'time {time}','wire',opportunity]]


def expected(commands,domain=0,opted=False):
    model = Model()
    rows = []
    for command in commands:
        if command.startswith('opportunity '):
            where,profile,context = map(int,command.split()[1:])
            allowed = context == 0 and (where == 0 or (profile != 0 and (domain == 0 or (profile == 2 and opted))))
            row = model.apply('step') if allowed else model.observe()
        else:
            row = model.apply(command)
        rows.append(row)
    return rows


def run(native,candidate,commands,domain=0,opted=False,target=None,verify=True):
    result = subprocess.run([str(native),candidate,str(domain),str(int(opted))],
                            input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert result.returncode == 0,result.stderr
    assert not result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(commands)
    if verify:
        target = expected(commands,domain,opted) if target is None else target
        for i,(actual,wanted) in enumerate(zip(rows,target)):
            assert {key:actual[key] for key in wanted} == wanted,(candidate,i,commands[i],actual,wanted)
            assert not actual['fault']
            physical = actual['active'] and actual['packet']['kind'] == 0
            assert actual['loan'] == int(not physical)
            assert actual['held'][0] == int(physical and not actual['terminal'])
            assert actual['resources'][1:] == ([1,7] if actual['held'][0] else [0,0])
            if commands[i].split()[0] not in ['step','opportunity','poll','wire']:
                assert actual['events'] == []
            if commands[i].startswith('wire') and i and rows[i-1]['active'] and not rows[i-1]['terminal'] and rows[i-1]['packet']['kind'] == 0:
                packet = rows[i-1]['packet']
                cancelled = any(c == 'cancel' for c in commands[(next((j for j in range(i-1,-1,-1) if rows[j]['started'] < rows[i-1]['started']),-1)+1):i])
                if not cancelled:
                    args = list(map(int,commands[i].split()[1:]))
                    prefix = args[1] if args and args[0] else len(packet['tx'])
                    wire = [packet['address']*2,*packet['tx'][:prefix]]
                    if packet['rx_size'] and not (args and args[0]):
                        wire.append(packet['address']*2+1)
                    assert actual['wire_tx'] == wire,(commands[i],actual['wire_tx'],wire)
    if output := os.environ.get('EXP06_X0_REGISTER_EVIDENCE'):
        key = hashlib.sha256(json.dumps([candidate,domain,opted,commands]).encode()).hexdigest()
        Path(output,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(dict(candidate=candidate,
            domain=domain,opted=opted,commands=commands,rows=rows),separators=(',',':')).encode(),mtime=0))
    return rows


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_whole_application_and_real_register_bytes(native,candidate):
    commands = ['attach','key 1','key 0','step',*cycles(2,3002)]
    rows = run(native,candidate,commands)
    assert rows[-1]['confirmed'] == 5020 and rows[-1]['mv'] == 5000 and rows[-1]['ma'] == 1000
    assert rows[-1]['frames'] >= 2 and rows[-1]['touch_done'] == 150


PHASES = {
    'command': ['step'],
    'bus-word': ['step',*cycles(2,4)],
    'data': ['step',*cycles(2,8)],
    'current-word': ['step',*cycles(2,10)],
}
CASES = [('command',0),('command',8),('command',9),('data',0),('data',16),('data',33),
         ('bus-word',0),('bus-word',1),('current-word',0),('current-word',1)]


@pytest.mark.parametrize('error',[0x100,0x200,0x400,0x800,0x1000])
@pytest.mark.parametrize('phase,prefix',CASES)
def test_error_prefix_resources_and_recovery(native,error,phase,prefix):
    commands = [*PHASES[phase],f'wire {error} {prefix}','step',*cycles(12,1200)]
    target = expected(commands)
    for candidate in ['R','A','B','C']:
        rows = run(native,candidate,commands,target=target)
        taken = rows[len(PHASES[phase])+1]
        assert taken['completion'] == [2,prefix,prefix,0,error]
        assert rows[-1]['frames'] > 0 and not rows[-1]['stale']


@pytest.mark.parametrize('candidate',['R','A','B','C'])
@pytest.mark.parametrize('dispatched',[False,True])
def test_cancellation_is_not_a_stopped_buffer_until_poll(native,candidate,dispatched):
    commands = ['step',*(['poll'] if dispatched else []),'cancel','cancel','time 20','wire','step',*cycles(22,1200)]
    rows = run(native,candidate,commands)
    before = commands.index('wire')-1
    assert rows[before]['loan'] == 0 and rows[before]['held'] == [1,0]
    assert rows[before+1]['loan'] == 0 and rows[before+1]['held'] == [0,0]
    assert rows[before+2]['completion'] == [3,0,0,0,0]


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_old_application_take_while_peer_owns_peripheral(native,candidate):
    commands = ['step','peer_open','wire','peer_open','peer_start','step','peer_finish','step',*cycles(2,1200)]
    rows = run(native,candidate,commands,verify=False)
    assert rows[1]['held'] == [1,0] # Peer cannot acquire before terminal/reap.
    assert rows[3]['held'] == rows[4]['held'] == [0,1] and rows[4]['loan'] == 0
    assert rows[5]['loan'] == 1 and rows[5]['held'] == [0,1]
    assert rows[5]['reclaimed'] == 1 and rows[5]['completion'] == [1,9,9,0,0]
    assert rows[5]['events'] == [] # Old take did not touch peer registers.
    assert rows[6]['held'] == [0,0] and not rows[-1]['fault'] and rows[-1]['frames'] > 0


@pytest.mark.parametrize('domain,opted',[(0,False),(1,False),(1,True)])
def test_opportunity_permissions_do_not_implicitly_poll(native,domain,opted):
    commands = ['attach','key 1','key 0','step','time 1000']
    commands += [f'opportunity 0 2 {context}' for context in [1,2,3]]
    commands += [f'opportunity {where} {profile} 0' for where in [1,2] for profile in [0,1,2]]
    commands += ['wire',*cycles(1002,2400)]
    target = expected(commands,domain,opted)
    for candidate in ['R','A','B','C']:
        rows = run(native,candidate,commands,domain,opted,target)
        assert rows[4]['completed'] == 0 and rows[4]['held'] == [1,0]


@pytest.mark.parametrize('seed',range(10))
def test_generated_faults_sessions_and_explicit_poll(native,seed):
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
            commands.append(f'wire {rng.choice([0,0x100,0x200,0x400,0x800,0x1000])} 0')
        elif kind == 6:
            commands.append(f'raw {rng.randrange(1000,2200)*8} {rng.randrange(65536)}')
        elif kind == 7:
            commands.append(f'opportunity {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}')
        elif kind == 8:
            commands.append('cancel')
        elif kind == 9:
            commands.append('poll')
        else:
            commands.append('step')
    commands += cycles(now+2,now+2402)
    target = expected(commands)
    for candidate in ['R','A','B','C']:
        run(native,candidate,commands,target=target)


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_failed_pin_readiness_rolls_back_and_clock_can_change_after_reap(native,candidate):
    commands = ['pins 0','step','step','pins 1','step','clock 12000000','wire','clock 12000000','step',*cycles(2,1200)]
    rows = run(native,candidate,commands,verify=False)
    assert all(row['started'] == 0 and row['loan'] == 1 and row['resources'][1:] == [0,0] and row['events'] == [] for row in rows[:3])
    assert rows[4]['started'] == 1 and rows[5]['resources'] == [48000000,1,7]
    assert rows[6]['terminal'] and rows[6]['loan'] == 0 and rows[6]['held'] == [0,0]
    assert rows[7]['resources'] == [12000000,0,0] and rows[7]['loan'] == 0
    assert rows[8]['reclaimed'] == 1 and rows[8]['resources'] == [12000000,1,7]
    assert rows[-1]['frames'] > 0 and not rows[-1]['fault']


def test_initialization_rejection_does_not_poison_resource_change(native):
    result = subprocess.run([str(native),'contracts'],capture_output=True,text=True,timeout=30)
    assert result.returncode == 0,result.stderr
