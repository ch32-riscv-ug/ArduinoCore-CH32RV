"""N07 reviewable selection files instantiate the already measured types."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

HERE=Path(__file__).parent
REPO=HERE.parents[2]


@pytest.mark.parametrize('exercise',['k0-two','display-change','touch-add'])
@pytest.mark.parametrize('changed',[False,True])
def test_selection_matches_historical_measured_type(exercise,changed,tmp_path):
    selector=REPO/'planning/core-redesign/tasks/EXP-07'/exercise/('after.cpp' if changed else 'before.cpp')
    source='#include "'+str(selector)+'"\n#include <type_traits>\n'
    if exercise=='k0-two':
        source+='struct Gate {using Token=bool;static void enter(Token&){}static void leave(Token){}static void normal(){}static void point(unsigned,unsigned){}};\n'
        n=2 if changed else 1
        for c in range(1,5):source+=f'static_assert(std::is_same<ExerciseApplication<{c},Gate>,experiment::K0Shared<{c},{n},Gate>>::value,"same instantiation");\n'
    elif exercise=='display-change':
        panel='display_experiment::Compact32' if changed else 'display_experiment::Mono64<32>'
        for c in range(1,5):source+=f'static_assert(std::is_same<ExerciseApplication<{c}>,experiment::DisplayX0<{c},{panel}>>::value,"same instantiation");\n'
    else:
        for c in range(1,5):source+=f'static_assert(std::is_same<ExerciseApplication<{c}>,experiment::FixedX0<{c},{str(changed).lower()}>>::value,"same instantiation");\n'
    compiler=shutil.which('g++');assert compiler
    command=[compiler,'-std=c++17','-Wall','-Wextra','-Werror','-fsyntax-only','-x','c++','-']
    p=subprocess.run(command,input=source,capture_output=True,text=True,timeout=60)
    if destination:=os.environ.get('EXP07_SELECTION_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=True)
        paths=[Path(__file__),selector,*sorted(HERE.glob('*.hpp'))]
        sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
        report=dict(exercise=exercise,changed=changed,command=command,stdin=source,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,compiler_sha256=sha(compiler),inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
        for path in paths:
            target=out/'inputs'/path.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        (out/f'{exercise}-{int(changed)}.json').write_text(json.dumps(report,indent=2)+'\n')
    assert p.returncode==0,p.stderr
