"""X0 RX-tail fault boundaries; unchanged firmware headers and whole-image oracle."""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
spec = importlib.util.spec_from_file_location('event_tail_reference',UNIT/'test_x0_event_wire.py')
event = importlib.util.module_from_spec(spec)
spec.loader.exec_module(event)


class Model(event.Model):
    def complete(self,success,word=None):
        current = self.current
        current['success'] = success
        current['due'] = self.now
        source = self.source
        if word is not None:
            quantity = 0 if current['payload'] == b'\x02' else 1
            self.source = tuple(word if i == quantity else v for i,v in enumerate(source))
        result = event.direct.reference.Model.apply(self,f'time {self.now}')
        self.source = source
        return result

    def apply(self,line):
        op,*words = line.split()
        args = list(map(int,words))
        if op == 'tail':
            self.status = 99
            stage,error = args
            assert self.current and self.current['owner'] == 2 and self.current['outcome'] is None
            if stage == 6:
                index = 0 if self.current['payload'] == b'\x02' else 1
                self.current['read_before_quiet'] = self.source[index]
                return self.observe()
            return self.complete(stage == 5)
        if op == 'quiesce':
            self.status = 99
            return self.complete(True,self.current['read_before_quiet'])
        return super().apply(line)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    directory = tmp_path_factory.mktemp('x0-tail')
    source = (UNIT/'i2c_master_driver.cpp').read_text()
    a = source.index('struct Fake {')
    b = source.index('template<bool Guarded',a)
    generated = directory/'i2c_handoff_fake.inc'
    generated.write_text(source[a:b])
    command = [compiler,'-std=c++17','-O1','-g1','-Wall','-Wextra','-Werror',
               '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
               '-I',str(directory),str(UNIT/'x0_rx_tail_driver.cpp'),'-o',str(directory/'app')]
    result = subprocess.run(command,capture_output=True,text=True,timeout=240)
    if output := os.environ.get('EXP06_X0_RX_TAIL_EVIDENCE'):
        out = Path(output)
        out.mkdir(parents=True,exist_ok=False)
        shutil.copy2(generated,out/generated.name)
        paths = [*[UNIT/name for name in ['x0_event_wire.hpp','completion_delivery.hpp','x0_register_wire.hpp','x0_wire.hpp',
                 'x0_features.hpp','x0_candidates.hpp','display_parts.hpp','r_application.hpp','r_workloads.hpp','data_parts.hpp',
                 'work_executor.hpp','event_executor.hpp','static_mailbox.hpp','i2c_managed_handoff.hpp','i2c_handoff.hpp','i2c_resources.hpp',
                 'x0_rx_tail_driver.cpp','x0_wire_oracle.py','app_oracle.py','oracle.py','i2c_master_driver.cpp',
                 'test_x0_event_wire.py','test_x0_register_wire.py']],Path(__file__),
                 REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp',REPO/'planning/core-redesign/x0-rx-tail-inputs.json']
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


def prefix(quantity):
    model = Model()
    commands = ['attach','key 1','key 0','step']
    for command in commands:
        model.apply(command)
    pointer = 2 if quantity == 'bus' else 4
    for time in range(2,1000,2):
        for command in [f'time {time}','wire','pump','deliver','step']:
            commands.append(command)
            model.apply(command)
        if model.measure_done and model.current and model.current['owner'] == 2 and model.current['payload'] == bytes([pointer]):
            return commands,time
    raise AssertionError('fixed second measurement was not reached')


def expected(commands,mode):
    model = Model(mode == 'event')
    return [model.apply(command) for command in commands]


def run(native,candidate,commands,mode,target=None):
    result = subprocess.run([str(native),mode,candidate,'0','0'],input='\n'.join(commands)+'\n',
                            capture_output=True,text=True,timeout=90)
    assert result.returncode == 0,result.stderr
    assert not result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    target = expected(commands,mode) if target is None else target
    assert len(rows) == len(target)
    for i,(actual,wanted) in enumerate(zip(rows,target)):
        assert {k:actual[k] for k in wanted} == wanted,(candidate,mode,i,commands[i],actual,wanted)
        assert not actual['fault']
        physical = actual['active'] and actual['packet']['kind'] == 0
        assert actual['loan'] == int(not physical)
        assert actual['held'][0] == int(physical and not actual['terminal'])
    if output := os.environ.get('EXP06_X0_RX_TAIL_EVIDENCE'):
        key = hashlib.sha256(json.dumps([mode,candidate,commands]).encode()).hexdigest()
        Path(output,f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(dict(mode=mode,candidate=candidate,
            commands=commands,rows=rows),separators=(',',':')).encode(),mtime=0))
    return rows


@pytest.mark.parametrize('candidate',['R','A','B','C'])
def test_original_wire_commands_still_match_whole_application(native,candidate):
    commands = ['attach','key 1','key 0','step',*event.cycles(2,1200)]
    for mode in ['direct','event']:
        rows = run(native,candidate,commands,mode)
        assert rows[-1]['mv'] == 5000 and rows[-1]['ma'] == 1000 and rows[-1]['frames'] > 0


@pytest.mark.parametrize('quantity',['bus','current'])
@pytest.mark.parametrize('stage',range(6))
@pytest.mark.parametrize('error',[0x100,0x200,0x400,0x800,0x1000])
def test_failure_before_or_after_rx_and_immutable_terminal(native,quantity,stage,error):
    before,time = prefix(quantity)
    commands = [*before,'raw 18000 65506',f'tail {stage} {error}','pump','deliver','step',*event.cycles(time+2,time+1200)]
    index = len(before)+1
    for mode in ['direct','event']:
        target = expected(commands,mode)
        for candidate in ['R','A','B','C']:
            rows = run(native,candidate,commands,mode,target)
            stopped,taken = rows[index],rows[index+3]
            assert stopped['loan'] == 0 and stopped['held'] == [0,0]
            assert taken['completion'] == [1 if stage == 5 else 2,1,1,2 if stage >= 4 else 0,0 if stage == 5 else error]
            reads = [e[2] for e in stopped['events'] if e[:2] == [0,16]]
            word = 18000 if quantity == 'bus' else 65506
            assert reads == ([] if stage < 2 else [word>>8] if stage == 2 else [word>>8,word&255])
            if stage < 5:
                assert taken['mv'] == 5000 and taken['ma'] == 1000 and taken['stale']
                assert taken['measurement_data'] == rows[len(before)-1]['measurement_data']
            assert rows[-1]['mv'] == 9000 and rows[-1]['ma'] == -30 and not rows[-1]['stale']


@pytest.mark.parametrize('quantity',['bus','current'])
def test_received_but_failed_pair_is_retained_through_full_and_rebind(native,quantity):
    before,time = prefix(quantity)
    commands = [*before,'raw 18000 65506','hint 0 0 0','tail 4 1024','pump','step','unbind','pump','bind',
                'pump','deliver','step',*event.cycles(time+2,time+1200)]
    index = len(before)+2
    target = expected(commands,'event')
    for candidate in ['R','A','B','C']:
        rows = run(native,candidate,commands,'event',target)
        assert rows[index+1]['status'] == 1 and rows[index+2]['loan'] == 0
        assert rows[index+4]['status'] == 2 and rows[index+4]['held'] == [0,0]
        assert rows[index+8]['completion'] == [2,1,1,2,1024] and rows[index+8]['stale']


@pytest.mark.parametrize('quantity',['bus','current'])
@pytest.mark.parametrize('mode',['direct','event'])
def test_successful_rx_does_not_release_until_quiet_and_uses_received_bytes(native,quantity,mode):
    before,time = prefix(quantity)
    commands = [*before,'raw 18000 65506','tail 6 0','raw 22000 2000','time 1000','pump','step','quiesce',
                'pump','deliver','step',*event.cycles(1002,2200)]
    index = len(before)+1
    target = expected(commands,mode)
    for candidate in ['R','A','B','C']:
        rows = run(native,candidate,commands,mode,target)
        assert rows[index+4]['loan'] == 0 and rows[index+4]['held'] == [1,0] and not rows[index+4]['terminal']
        assert rows[index+5]['loan'] == 0 and rows[index+5]['held'] == [0,0] and rows[index+5]['terminal']
        assert rows[index+8]['completion'] == [1,1,1,2,0]
        if quantity == 'current':
            assert rows[index+8]['mv'] == 5000 and rows[index+8]['ma'] == -30
