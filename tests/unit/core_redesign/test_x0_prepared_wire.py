"""X035 setup/route readiness in the complete X0 direct/event applications."""
from concurrent.futures import ThreadPoolExecutor, as_completed
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
ROUTES = json.loads((REPO/'planning/core-redesign/x035-i2c-route-inputs.json').read_text())
SUPPORTED = [(p,r) for p in range(2) for r in range(8) if not (p == 1 and r < 2)]
spec = importlib.util.spec_from_file_location('prepared_application_reference',UNIT/'test_x0_event_wire.py')
event = importlib.util.module_from_spec(spec)
spec.loader.exec_module(event)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@pytest.fixture(scope='module')
def native(tmp_path_factory):
    compiler = shutil.which('g++')
    assert compiler
    directory = tmp_path_factory.mktemp('x0-prepared')
    source = (UNIT/'i2c_master_driver.cpp').read_text()
    a = source.index('struct Fake {')
    b = source.index('template<bool Guarded',a)
    board = (UNIT/'x035_setup_fake.hpp').read_text().replace('words[6]={3,0,0,0,0,0}','words[7]={3,0,0,0,0,0,0}')
    board = board.replace('Remap,PinConfig};','Remap,PinConfig,0x4001101cu};').replace('i<6','i<7')
    board = board.replace('if(i==5)assert(words[2]&4);','if(i==5)assert(words[2]&4);\n        if(i==6)assert(words[2]&16);')
    generated = {'i2c_handoff_fake.inc':source[a:b],'x035_route_fake.inc':board}
    for name,text in generated.items():
        (directory/name).write_text(text)
    out = None
    if output := os.environ.get('EXP06_X0_PREPARED_EVIDENCE'):
        out = Path(output)
        out.mkdir(parents=True,exist_ok=False)
        for name in generated:
            shutil.copy2(directory/name,out/name)
    paths = [*[UNIT/name for name in ['x0_prepared_wire.hpp','x035_i2c_routes.hpp','x035_i2c_setup.hpp','x035_setup_fake.hpp',
             'x0_event_wire.hpp','completion_delivery.hpp','x0_register_wire.hpp','x0_wire.hpp','x0_features.hpp','x0_candidates.hpp',
             'display_parts.hpp','r_application.hpp','r_workloads.hpp','data_parts.hpp','work_executor.hpp','event_executor.hpp',
             'static_mailbox.hpp','i2c_managed_handoff.hpp','i2c_handoff.hpp','i2c_resources.hpp','x0_prepared_wire_driver.cpp',
             'x0_wire_oracle.py','app_oracle.py','oracle.py','i2c_master_driver.cpp','test_x0_event_wire.py','test_x0_register_wire.py']],
             Path(__file__),REPO/'tests/platform/core_redesign/runtime/i2c_master.hpp',
             REPO/'planning/core-redesign/x0-prepared-wire-inputs.json',REPO/'planning/core-redesign/x035-i2c-route-inputs.json']
    report = dict(compiler_sha256=sha(compiler),compiler_version=subprocess.check_output([compiler,'--version'],text=True),
                  generated={n:sha(directory/n) for n in generated},inputs={str(p.relative_to(REPO)):sha(p) for p in paths},builds=[])
    if out:
        for path in paths:
            target = out/'inputs'/path.relative_to(REPO)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,target)
    def build(package,route):
        executable = directory/f'p{package}-r{route}'
        command = [compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror',
                   '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                   f'-DROUTE={route}',f'-DPACKAGE_ID={package}','-I',str(directory),
                   str(UNIT/'x0_prepared_wire_driver.cpp'),'-o',str(executable)]
        result = subprocess.run(command,capture_output=True,text=True,timeout=240)
        item = dict(name=executable.name,command=command,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr)
        if result.returncode == 0:
            item['executable_sha256'] = sha(executable)
            if out:
                shutil.copy2(executable,out/executable.name)
        return (package,route),executable,item
    binaries = {}
    with ThreadPoolExecutor(max_workers=2) as pool:
        for future in as_completed([pool.submit(build,p,r) for p in range(2) for r in range(8)]):
            key,exe,item = future.result()
            report['builds'].append(item)
            report['builds'].sort(key=lambda v:v['name'])
            if out:
                (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
            assert item['returncode'] == 0,item['stderr']
            binaries[key] = exe
    return binaries,out


def run(native,commands,candidate='R',mode='prepared-event',package=0,route=0,oracle=False):
    binaries,out = native
    result = subprocess.run([str(binaries[package,route]),mode,candidate,'0','0'],input='\n'.join(commands)+'\n',
                            capture_output=True,text=True,timeout=90)
    assert result.returncode == 0,result.stderr
    assert not result.stderr
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == len(commands)
    if oracle:
        model = event.Model(mode.endswith('event'))
    for i,(command,row) in enumerate(zip(commands,rows)):
        assert not row['fault']
        physical = row['active'] and row['packet']['kind'] == 0
        assert row['loan'] == int(not physical)
        count = sum(row['held'])+row['preparation'][1]
        assert row['resources'][1:] == ([1,7] if count else [0,0])
        assert count <= 1
        if oracle:
            wanted = model.apply(command)
            assert {k:row[k] for k in wanted} == wanted,(candidate,mode,i,command,row,wanted)
    if out:
        key = hashlib.sha256(json.dumps([package,route,mode,candidate,commands]).encode()).hexdigest()
        (out/f'{key}.json.gz').write_bytes(gzip.compress(json.dumps(dict(package=package,route=route,mode=mode,
            candidate=candidate,commands=commands,rows=rows),separators=(',',':')).encode(),mtime=0))
    return rows


def setup():
    return ['set32 4 67108864','prepare 1']


def check_final(row):
    render = event.direct.reference.base
    assert row['confirmed'] == 5020 and row['mv'] == 5000 and row['ma'] == 1000 and not row['stale']
    assert row['displayed_hash'] == render.fnv(render.render_frame(5020,5020,5000,1000,True,False,True))
    assert row['frames'] > 0


@pytest.mark.parametrize('package,route',SUPPORTED)
@pytest.mark.parametrize('kind',['direct','event'])
def test_prepared_application_matches_boolean_and_whole_image(native,package,route,kind):
    body = ['attach','key 1','key 0','step',*event.cycles(2,1200)]
    for candidate in ['R','A','B','C']:
        rows = run(native,[*setup(),*body],candidate,'prepared-'+kind,package,route)
        assert rows[1]['status'] == 0 and rows[1]['preparation'] == [1,0]
        baseline = run(native,body,candidate,'boolean-'+kind,package,route,oracle=True)
        for i,(row,old) in enumerate(zip(rows[2:],baseline)):
            assert {k:v for k,v in row.items() if k not in ['preparation','events','board_events']} == {k:v for k,v in old.items() if k not in ['preparation','events','board_events']},(candidate,package,route,i)
            if body[i] == 'wire':
                assert row['events'] == old['events']
        check_final(rows[-1])


@pytest.mark.parametrize('route',[0,1])
@pytest.mark.parametrize('kind',['direct','event'])
def test_unsupported_f7_route_never_prepares_or_accepts_wire(native,route,kind):
    commands = [*setup(),'attach','key 1','key 0','step','time 1000','step','ready']
    for candidate in ['R','A','B','C']:
        rows = run(native,commands,candidate,'prepared-'+kind,1,route)
        assert rows[1]['status'] == 6 and rows[1]['events'] == rows[1]['board_events'] == []
        assert rows[-1]['status'] == 0 and rows[-1]['started'] == 0 and rows[-1]['loan'] == 1
        assert all(row['events'] == row['board_events'] == [] for row in rows)


@pytest.mark.parametrize('package,route',SUPPORTED)
@pytest.mark.parametrize('kind',['direct','event'])
@pytest.mark.parametrize('fault',['clock','gate','route','pin'])
def test_open_rechecks_board_then_recovers(native,package,route,kind,fault):
    mapping = ROUTES['routes'][route]
    index,value,restore = {'clock':(1,48,0),'gate':(3,0,0x00200000),
                           'route':(4,0x04000000|((route^1)<<2),0x04000000|(route<<2)),
                           'pin':(5 if route<2 else 6,0,mapping['mode'])}[fault]
    commands = [*setup(),f'set32 {index} {value}','attach','key 1','key 0','step',
                f'set32 {index} {restore}','prepare 1','step',*event.cycles(2,1200)]
    for candidate in ['R','A','B','C']:
        rows = run(native,commands,candidate,'prepared-'+kind,package,route)
        assert rows[6]['started'] == 0 and rows[6]['loan'] == 1 and rows[6]['held'] == [0,0]
        assert not any(e[0] == 1 for e in rows[6]['events']) and rows[6]['resources'][1:] == [0,0]
        assert rows[8]['status'] == 0 and rows[9]['started'] == 1
        check_final(rows[-1])


@pytest.mark.parametrize('kind',['direct','event'])
@pytest.mark.parametrize('ignored',[2,3,4,5])
def test_partial_prepare_failure_holds_lease_until_abandon(native,kind,ignored):
    commands = ['set32 4 67108892',f'ignore {1<<ignored}','prepare 1','prepare 1','step',
                'abandon 0','ignore 0','abandon 1','prepare 1','attach','key 1','key 0','step',*event.cycles(2,1200)]
    for candidate in ['R','A','B','C']:
        rows = run(native,commands,candidate,'prepared-'+kind)
        assert rows[2]['status'] == 4 and rows[2]['preparation'] == [0,1]
        assert rows[3]['status'] == 1 and rows[4]['started'] == 0
        assert rows[4]['events'] == rows[4]['board_events'] == []
        assert rows[5]['status'] == 0 and rows[7]['status'] == 1
        assert rows[8]['status'] == 0 and rows[8]['resources'][1:] == [0,0]
        check_final(rows[-1])


@pytest.mark.parametrize('kind',['direct','event'])
def test_debug_is_not_disabled_implicitly(native,kind):
    commands = ['prepare 1','abandon 1','set32 4 67108864','prepare 1','attach','key 1','key 0','step',*event.cycles(2,1200)]
    for candidate in ['R','A','B','C']:
        rows = run(native,commands,candidate,'prepared-'+kind,0,3)
        assert rows[0]['status'] == 4 and rows[0]['board'][4] == 0
        assert not any(e[0] == 1 and e[1] == 0x40010004 for e in rows[0]['board_events'])
        assert rows[3]['status'] == 0
        check_final(rows[-1])


@pytest.mark.parametrize('package,route',SUPPORTED)
@pytest.mark.parametrize('kind',['direct','event'])
def test_old_result_survives_reprepare_and_peer_at_new_clock(native,package,route,kind):
    commands = [*setup(),'step','prepare 1','wire','clock 12000000','prepare 1','set32 1 48','prepare 1',
                'peer_open','peer_start','pump','deliver','step','peer_finish','attach','key 1','key 0','step',*event.cycles(2,1200)]
    for candidate in ['R','A','B','C']:
        rows = run(native,commands,candidate,'prepared-'+kind,package,route)
        assert rows[3]['status'] == 2 and rows[3]['events'] == rows[3]['board_events'] == []
        assert rows[6]['status'] == 3 and rows[6]['resources'][1:] == [0,0] and rows[6]['loan'] == 0
        assert rows[8]['status'] == 0 and rows[8]['loan'] == 0
        assert rows[13]['reclaimed'] == 1 and rows[13]['loan'] == 1 and rows[13]['held'] == [0,1]
        assert rows[13]['events'] == rows[13]['board_events'] == []
        assert rows[18]['resources'] == [12000000,1,7]
        assert [1,4,12] in rows[18]['events'] and [1,28,0x800a] in rows[18]['events']
        check_final(rows[-1])
