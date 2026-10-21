"""Typed refusal/output preservation and local progress policy of N06 adapter."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE=Path(__file__).parent
REPO=HERE.parents[2]


def test_transport_refusals_permit_identity_and_progress_policy(tmp_path):
    compiler=shutil.which('g++');assert compiler
    binary=tmp_path/'contract'
    command=[compiler,'-std=c++17','-O0','-g1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(HERE/'h417_transport_contract_driver.cpp'),'-o',str(binary)]
    p=subprocess.run(command,capture_output=True,text=True,timeout=180)
    paths=[Path(__file__),*[HERE/n for n in ['h417_transport_contract_driver.cpp','h417_transport.hpp','h417_endpoint.hpp','h417_features.hpp','event_executor.hpp','work_executor.hpp','data_parts.hpp']]]
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    report=dict(command=command,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,compiler_sha256=sha(compiler),
                inputs={str(p.relative_to(REPO)):sha(p) for p in paths})
    out=None
    if destination:=os.environ.get('EXP06_H417_TRANSPORT_CONTRACT_EVIDENCE'):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        for path in paths:
            target=out/'inputs'/path.relative_to(REPO);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        if not p.returncode:shutil.copy2(binary,out/'contract');report['executable_sha256']=sha(binary)
        (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    assert p.returncode==0,p.stderr
    p=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
    if out:(out/'run.json').write_text(json.dumps(dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr),indent=2)+'\n')
    assert p.returncode==0,p.stderr;assert not p.stderr
    assert json.loads(p.stdout)==dict(transport_sequence=1,policy_cases=72,context_cases=12)
