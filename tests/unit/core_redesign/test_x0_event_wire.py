"""N02 event hints in the complete X0 application; independent delivery ledger."""
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
spec = importlib.util.spec_from_file_location('direct_register_reference',UNIT/'test_x0_register_wire.py')
direct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(direct)


class Model(direct.Model):
    def __init__(self,enabled=True):
        super().__init__()
        self.enabled = enabled
        self.generation = int(enabled)
        self.bound = enabled
        self.queue = []
        self.ready = False
        self.calls = self.matched = self.skipped = 0
        self.status = 99
        self.domain, self.opted = 0, False
        self.sequences = [0,0,0]

    def step(self):
        old = self.current
        blocked = self.enabled and old and old['kind'] == 0 and old['outcome'] is not None and not self.ready
        if blocked:
            outcome = old['outcome']
            old['outcome'] = None
            super().step()
            old['outcome'] = outcome
        else:
            super().step()
        if self.current is not old:
            self.ready = False
            if self.current and self.current['kind'] == 0:
                client = self.current['owner']-1
                self.sequences[client] += 1
                self.current['request'] = (self.sequences[client],client,client)

    def apply(self,line):
        op,*words = line.split()
        args = list(map(int,words))
        self.status = 99
        if op not in ['pump','deliver','unbind','bind','hint']:
            return super().apply(line)
        if not self.enabled:
            return self.observe()
        physical = self.current and self.current['kind'] == 0
        pending = physical and self.current['outcome'] is not None
        if op == 'pump':
            if not pending:
                self.status = 3
            elif not self.bound:
                self.status = 2
            elif self.queue:
                self.status = 1
            else:
                self.queue.append(self.current['request'])
                self.status = 0
        elif op == 'hint':
            self.status = 2 if not self.bound else 1 if self.queue else 0
            if self.status == 0:
                self.queue.append(tuple(args))
        elif op == 'unbind':
            self.status = int(self.bound)
            if self.bound:
                self.bound = False
                self.queue.clear()
                self.ready = False
        elif op == 'bind':
            self.status = int(not self.bound)
            if not self.bound:
                self.generation += 1
                self.bound = True
                self.ready = False
                self.domain,self.opted = args if args else [0,False]
        else:
            where,profile,context = args if args else [0,0,0]
            self.status = 2 if context in [1,3] else 3 if context == 2 else 4 if where and profile == 0 else 0
            eligible = not where or self.domain == 0 or (profile == 2 and self.opted)
            if self.status == 0 and self.bound and self.queue and eligible:
                hint = self.queue.pop(0)
                self.calls += 1
                if pending and hint == self.current['request']:
                    self.ready = True
                    self.matched += 1
                else:
                    self.skipped += 1
        return self.observe()

    def observe(self):
        row = super().observe()
        row.update(status=self.status,notification=[self.generation,int(self.bound),len(self.queue),int(self.ready),
                                                   self.calls,self.matched,self.skipped] if self.enabled else [0]*7)
        return row


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    directory = tmp_path_factory.mktemp('x0-event')
    source = (UNIT/'i2c_master_driver.cpp').read_text()
    a = source.index('struct Fake {')
    b = source.index('template<bool Guarded',a)
    generated = directory/'i2c_handoff_fake.inc'
    generated.write_text(source[a:b])
    command = [compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror',
               '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
               '-I',str(directory),str(UNIT/'x0_event_wire_driver.cpp'),'-o',str(directory/'app')]
    result = subprocess.run(command,capture_output=True,text=True,timeout=240)
    if output := os.environ.get('EXP06_X0_EVENT_EVIDENCE'):
        out = Path(output)
        out.mkdir(parents=True,exist_ok=False)
        shutil.copy2(generated,out/generated.name)
        paths = [*[UNIT/name for name in ['x0_event_wire.hpp','completion_delivery.hpp','x0_register_wire.hpp','x0_wire.hpp',
                 'x0_features.hpp','x0_candidates.hpp','display_parts.hpp','r_application.hpp','r_workloads.hpp','data_parts.hpp',
                 'work_executor.hpp','event_executor.hpp','static_mailbox.hpp','i2c_managed_handoff.hpp','i2c_handoff.hpp','i2c_resources.hpp',
                 'x0_event_wire_driver.cpp','x0_wire_oracle.py','app_oracle.py','oracle.py','i2c_master_driver.cpp','test_x0_register_wire.py']],
                 Path(__file__),REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp',
                 REPO/'planning/core-redesign/x0-event-wire-inputs.json']
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


def cycles(first,last):
    return [command for time in range(first,last,2) for command in [f'time {time}','wire','pump','deliver','step']]


def expected(commands,enabled=True,domain=0,opted=False):
    model = Model(enabled)
    rows = []
    for command in commands:
        if command.startswith('opportunity '):
            where,profile,context = map(int,command.split()[1:])
            allowed = context == 0 and (where == 0 or (profile != 0 and (domain == 0 or (profile == 2 and opted))))
            row = model.apply('step') if allowed else model.observe()
            row['status'] = 99
        else:
            row = model.apply(command)
        rows.append(row)
    return rows


def run(native,candidate,commands,mode='event',domain=0,opted=False,verify=True,target=None):
    result = subprocess.run([str(native),mode,candidate,str(domain),str(int(opted))],
                            input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
    assert result.returncode == 0,result.stderr
    assert not result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(commands)
    if verify:
        target = expected(commands,mode == 'event',domain,opted) if target is None else target
        for i,(actual,wanted) in enumerate(zip(rows,target)):
            assert {k:actual[k] for k in wanted} == wanted,(candidate,mode,i,commands[i],actual,wanted)
            assert not actual['fault']
            physical = actual['active'] and actual['packet']['kind'] == 0
            assert actual['loan'] == int(not physical)
            assert actual['held'][0] == int(physical and not actual['terminal'])
            assert actual['resources'][1:] == ([1,7] if actual['held'][0] else [0,0])
            if commands[i].split()[0] in ['pump','deliver','unbind','bind','hint']:
                assert actual['events'] == []
    if output := os.environ.get('EXP06_X0_EVENT_EVIDENCE'):
        key = hashlib.sha256(json.dumps([mode,candidate,domain,opted,commands]).encode()).hexdigest()
        Path(output,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(dict(mode=mode,candidate=candidate,
            domain=domain,opted=opted,commands=commands,rows=rows),separators=(',',':')).encode(),mtime=0))
    return rows


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_same_application_and_mmio_with_explicit_delivery(native,candidate):
    commands = ['attach','key 1','key 0','step',*cycles(2,3002)]
    event = run(native,candidate,commands)
    control = run(native,candidate,commands,'direct')
    for actual,baseline in zip(event,control):
        assert {k:v for k,v in actual.items() if k not in ['status','notification']} == {k:v for k,v in baseline.items() if k not in ['status','notification']}
    assert event[-1]['confirmed'] == 5020 and event[-1]['measure_done'] == 30 and event[-1]['touch_done'] == 150


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_queue_full_unbind_and_retry_keep_the_result(native,candidate):
    commands = ['step','hint 0 0 0','wire','pump','step','time 1000','step','deliver','pump','unbind',
                'deliver','pump','bind','pump','deliver','step',*cycles(1002,2400)]
    rows = run(native,candidate,commands)
    assert rows[3]['status'] == 1 and rows[6]['reclaimed'] == 0
    assert rows[6]['loan'] == 0 and rows[6]['held'] == [0,0]
    assert rows[7]['notification'][4:] == [1,0,1]
    assert rows[11]['status'] == 2 and rows[11]['loan'] == 0
    assert rows[14]['notification'][3] and rows[15]['reclaimed'] == 1
    assert rows[-1]['frames'] > 0


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_duplicate_old_hint_cannot_collect_the_next_request(native,candidate):
    commands = ['step','wire','pump','pump','deliver','pump','step','wire','deliver','pump','deliver','step',*cycles(2,1200)]
    rows = run(native,candidate,commands)
    assert rows[3]['status'] == 1 and rows[6]['reclaimed'] == 1
    assert rows[8]['notification'][4:] == [2,1,1] and rows[8]['loan'] == 0
    assert rows[8]['reclaimed'] == 1 and rows[10]['notification'][3]
    assert rows[11]['reclaimed'] == 2


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_notification_and_old_take_do_not_touch_peer_hardware(native,candidate):
    commands = ['step','wire','pump','peer_open','peer_start','step','deliver','step','peer_finish','step',*cycles(2,1200)]
    rows = run(native,candidate,commands,verify=False)
    assert rows[5]['reclaimed'] == 0 and rows[5]['loan'] == 0 and rows[5]['held'] == [0,1]
    assert rows[6]['notification'][3] and rows[6]['events'] == [] and rows[6]['held'] == [0,1]
    assert rows[7]['reclaimed'] == 1 and rows[7]['loan'] == 1 and rows[7]['events'] == [] and rows[7]['held'] == [0,1]
    assert rows[-1]['frames'] > 0 and not rows[-1]['fault']


@pytest.mark.parametrize('domain,opted',[(0,False),(1,False),(1,True)])
def test_delivery_permissions_and_no_implicit_user_reentry(native,domain,opted):
    commands = ['unbind',f'bind {domain} {int(opted)}','step','wire','pump']
    commands += [f'deliver 0 2 {context}' for context in [1,2,3]]
    commands += [f'deliver {where} {profile} 0' for where in [1,2] for profile in [0,1,2]]
    commands += ['deliver','step',*cycles(2,1200)]
    target = expected(commands)
    for candidate in ['R','A','B','C']:
        rows = run(native,candidate,commands,target=target)
        assert [rows[i]['status'] for i in [5,6,7]] == [2,3,2]
        assert all(rows[i]['reclaimed'] == 0 and rows[i]['loan'] == 0 for i in range(5,8))


@pytest.mark.parametrize('error',[0x100,0x200,0x400,0x800,0x1000])
@pytest.mark.parametrize('phase',['command','data','bus-word','current-word'])
def test_error_results_remain_held_until_hint_and_take(native,error,phase):
    prefix = {'command':['step'],'bus-word':['step',*cycles(2,4)],
              'data':['step',*cycles(2,8)],'current-word':['step',*cycles(2,10)]}[phase]
    commands = [*prefix,f'wire {error} 0','pump','step','deliver','step',*cycles(12,1200)]
    target = expected(commands)
    for candidate in ['R','A','B','C']:
        rows = run(native,candidate,commands,target=target)
        i = len(prefix)
        assert rows[i+2]['loan'] == 0 and rows[i+4]['completion'] == [2,0,0,0,error]
        assert rows[-1]['frames'] > 0 and not rows[-1]['stale']


@pytest.mark.parametrize('seed',range(10))
def test_generated_delivery_loss_and_progress(native,seed):
    rng = random.Random(seed)
    commands = []
    now = 0
    for _ in range(10000):
        kind = rng.randrange(17)
        if kind == 0: commands.append('attach')
        elif kind == 1: commands.append('detach')
        elif kind <= 3: commands.append(f'key {rng.choice([-1,0,1])}')
        elif kind == 4:
            now += rng.randrange(30)
            commands.append(f'time {now}')
        elif kind == 5: commands.append(f'wire {rng.choice([0,0x100,0x200,0x400,0x800,0x1000])} 0')
        elif kind == 6: commands.append(f'raw {rng.randrange(1000,2200)*8} {rng.randrange(65536)}')
        elif kind == 7: commands.append(f'deliver {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}')
        elif kind == 8: commands.append('cancel')
        elif kind == 9: commands.append('poll')
        elif kind == 10: commands.append('pump')
        elif kind == 11: commands.append('unbind')
        elif kind == 12: commands.append(f'bind {rng.randrange(2)} {rng.randrange(2)}')
        elif kind == 13: commands.append('hint 0 0 0')
        else: commands.append('step')
    commands += ['unbind','bind',*cycles(now+2,now+2402)]
    target = expected(commands)
    for candidate in ['R','A','B','C']:
        run(native,candidate,commands,target=target)


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_notification_stall_does_not_prevent_pps_control(native,candidate):
    commands = ['step','hint 0 0 0','wire','pump','attach','key 1','key 0','time 1000','step',
                'time 1010','step','deliver','pump','deliver','step',*cycles(1012,2600)]
    rows = run(native,candidate,commands)
    control = run(native,candidate,commands,'direct')
    assert rows[10]['confirmed'] == 5020 and rows[10]['pd_reclaimed'] == 1
    assert rows[10]['reclaimed'] == rows[10]['measure_done'] == rows[10]['touch_done'] == 0
    assert rows[10]['loan'] == 0 and rows[10]['held'] == [0,0]
    assert control[10]['reclaimed'] == 1
    assert rows[-1]['frames'] > 0 and rows[-1]['measure_done'] > 0
