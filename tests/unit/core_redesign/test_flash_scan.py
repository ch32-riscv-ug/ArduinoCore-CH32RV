"""Bounded physical reads and cache lifetime under a shared serialized region table."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

REPO=Path(__file__).resolve().parents[3]
UNIT=Path(__file__).parent
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);loaded=importlib.util.module_from_spec(spec);spec.loader.exec_module(loaded);return loaded
media=module("scan_medium",UNIT/"flash_model.py")
formats=module("scan_reference",UNIT/"flash_formats.py")
config=json.loads((REPO/"planning/core-redesign/flash-format-inputs.json").read_text())
geometry=json.loads((REPO/"planning/core-redesign/flash-inputs.json").read_text())
CASES=[(t["part"],mode,g["program_unit"],g["erase_unit"],u,i,c) for t in geometry["targets"] for mode,g in t["modes"].items() for u in config["erase_units"] for i,c in enumerate(config["candidates"])]
BASE=0x08000000


def make(case):
    m=media.Medium(case[2],case[3],case[4]);c=case[6]
    return formats.Store(m,kind=c["kind"],policy=c["policy"],commit=c["commit"])
def case_id(case):return f"{case[0]}:{case[1]}:{case[4]}:{case[6]['id']}"
SUPPORTED=[case for case in CASES if make(case).layout()["supported"]]
UNSUPPORTED=[case for case in CASES if not make(case).layout()["supported"]]


@pytest.fixture(scope="module")
def scan_native(tmp_path_factory):
    compiler=shutil.which("g++");assert compiler
    exe=tmp_path_factory.mktemp("flash-scan")/"control"
    p=subprocess.run([compiler,"-std=c++17","-O1","-g1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer",str(UNIT/"flash_scan_driver.cpp"),"-o",str(exe)],capture_output=True,text=True,timeout=300)
    assert p.returncode==0,p.stderr
    if destination:=os.environ.get("EXP04_SCAN_EVIDENCE"):
        out=Path(destination);out.mkdir(parents=True,exist_ok=False)
        shutil.copyfile(exe,out/"native-control")
        sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
        sources=[UNIT/"flash_scan_driver.cpp",UNIT/"flash_scan.hpp",UNIT/"flash_store.hpp",Path(__file__)]
        record=dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,
                    compiler=compiler,compiler_sha256=sha(compiler),binary_sha256=sha(exe),
                    flags=["-std=c++17","-O1","-g1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer"],
                    inputs={str(path.relative_to(REPO)):sha(path) for path in sources})
        (out/"native-build.json").write_text(json.dumps(record,indent=2)+"\n")
    return exe


def execute(exe,args,commands):
    p=subprocess.run([str(exe),*map(str,args)],input="\n".join(commands)+"\n",capture_output=True,text=True,timeout=90)
    assert p.returncode==0,p.stderr;assert not p.stderr
    rows=[json.loads(line) for line in p.stdout.splitlines()];assert len(rows)==len(commands)
    return rows


def read(exe,case,commands):
    rows=execute(exe,[case[5],case[2],case[3],case[4]],commands);limit=case[3]*case[4]
    for command,row in zip(commands,rows):
        if command.startswith("poll "):
            assert len(row["reads"])<=min(int(command.split()[1]),32)
            if row["reads"]:assert row["owned"] and all(BASE<=p<BASE+limit for p in row["reads"])
        else:assert not row["reads"],(command,row)
        if row["ready"]:assert row["owned"] and row["phase"]==2
    return rows


def result(row):
    statuses=["empty","valid","corrupt","ambiguous","unvalidated","no-space"]
    return dict(status=statuses[row["load"]],generation=row["generation"],bank=None if row["bank"]==255 else row["bank"],payload=bytes(row["payload"]))


def expected(store):
    s=store.load();v=s["value"]
    payload=b"" if v is None else (b"".join(struct.pack("<HI",k,v[k]) for k in sorted(v)) if isinstance(v,dict) else v)
    return dict(status=s["status"],generation=s["generation"],bank=s["bank"],payload=payload)


def seed_commands(case,image):
    commands=[f"writer 0 {case[3]*case[4]}"]
    commands += [f"poke {i} {byte}" for i,byte in enumerate(image) if byte!=255]
    return commands+["release 1"]


@pytest.mark.parametrize("case",SUPPORTED,ids=case_id)
@pytest.mark.parametrize("budget",[1,7,32,0xffffffff])
def test_empty_and_valid_snapshots_match_python_with_bounded_reads(scan_native,case,budget):
    s=make(case);quantum=min(budget,32);limit=case[3]*case[4]
    # First scan truly erased contents; no hidden reads on begin/peek/end.
    calls=(limit+100+quantum-1)//quantum
    empty=read(scan_native,case,["begin","poll 0","peek",*[f"poll {budget}"]*calls,"peek","end","peek"])
    assert empty[0]["status"]==0 and empty[1]["phase"]==1
    assert empty[2]["status"]==0 and empty[2]["out"]==[77,2,99,8,*([42]*12)]
    assert result(empty[-3])==expected(s) and empty[-3]["status"]==1
    assert empty[-1]["status"]==0 and empty[-1]["out"]==empty[-3]["out"]
    for n in [1,2]:
        value={7:119*n,11:991*n} if s.kind=="keys" else (b"first-v1" if n==1 else b"old--v2!")
        s.begin(value)
        while s.active is not None:s.step()
    commands=seed_commands(case,bytes(s.medium.data))+["begin",*[f"poll {budget}"]*calls,"peek"]
    valid=read(scan_native,case,commands)
    assert valid[-1]["status"]==1 and result(valid[-1])==expected(s)
    assert sum(len(row["reads"]) for row in valid)<=limit+100


@pytest.mark.parametrize("case",UNSUPPORTED,ids=case_id)
def test_no_space_does_not_claim_or_read(scan_native,case):
    rows=read(scan_native,case,["begin","poll 32","peek","end"])
    assert rows[0]["status"]==4 and all(not r["owned"] and not r["occupied"] and not r["reads"] for r in rows)


def frame(kind,generation,payload):
    prefix=struct.pack("<4sIHH",b"KEY1" if kind=="keys" else b"BLB1",generation,len(payload),1)
    return prefix+struct.pack("<I",zlib.crc32(prefix+payload)&0xffffffff)+payload


@pytest.mark.parametrize("commit",[False,True])
def test_crc_schema_ambiguity_and_dirt_after_erased_header(scan_native,commit):
    case=next(c for c in SUPPORTED if c[2]==64 and c[3]==64 and c[4]==4 and c[6]["kind"]=="keys" and c[6]["policy"]=="two-bank" and c[6]["commit"]==commit)
    for variant in ["valid","duplicate-key","ambiguous","wrong-crc","dirty-tail","wrong-version","bad-footer"]:
        s=make(case);payload=struct.pack("<HIHI",7,1,7 if variant=="duplicate-key" else 11,2)
        f=bytearray(frame("keys",3,payload))
        if variant=="wrong-crc":f[12]^=1
        if variant=="wrong-version":f[10]=2
        if variant=="dirty-tail":s.medium.data[s.bank_bytes-1]=0
        else:
            s.medium.data[:len(f)]=f
            if commit:s.medium.data[s.bank_bytes-s.footer_bytes:s.bank_bytes-s.footer_bytes+12]=b"END1"+struct.pack("<II",3,int.from_bytes(f[12:16],"little"))
            if commit and variant=="bad-footer":s.medium.data[s.bank_bytes-s.footer_bytes]^=1
            if variant=="ambiguous":s.medium.data[s.bank_bytes:]=s.medium.data[:s.bank_bytes]
        rows=read(scan_native,case,seed_commands(case,bytes(s.medium.data))+["begin",*["poll 7"]*60,"peek"])
        assert rows[-1]["status"]==1 and result(rows[-1])==expected(s)


def test_shared_lease_blocks_overlap_allows_disjoint_and_invalidates_cache(scan_native):
    case=next(c for c in SUPPORTED if c[2]==64 and c[3]==64 and c[4]==2 and c[6]["id"]=="blob-two-bank")
    commands=["begin","poll 7","writer 0 128","writer 64 128","writer 128 128","poke 0 0","configure 134217728 256",*["poll 32"]*8,"peek","end","peek","release 2","writer 0 128"]
    encoded=frame("blob",1,b"changed!")
    commands += [f"poke {i} {v}" for i,v in enumerate(encoded)]+["release 3","begin",*["poll 32"]*8,"peek","revoke 4","peek","writer 0 128","poll 32","end","release 4","release 5","begin"]
    rows=read(scan_native,case,commands)
    assert rows[2]["status"]==rows[3]["status"]==1 and rows[4]["status"]==0
    assert rows[5]["status"]==1 and rows[6]["status"]==0
    assert rows[15]["status"]==1 and rows[17]["status"]==0
    changed=next(row for command,row in zip(commands,rows) if command=="revoke 4")
    assert changed["status"]==1 and not changed["ready"]
    peek_index=commands.index("revoke 4")+1
    assert rows[peek_index]["status"]==0 and rows[peek_index]["payload"]==[]
    assert rows[peek_index]["out"]==rows[peek_index-2]["out"]
    assert rows[-5]["phase"]==3 and not rows[-5]["reads"]
    assert rows[-3]["status"]==0 and rows[-2]["status"]==1 and rows[-1]["status"]==0


def test_ledger_bounds_tokens_and_configure_do_not_reset_sequence(scan_native):
    commands=["configure 134217728 1024","claim 0 134217728 64 64","claim 1 134217760 64 64","claim 1 134217728 64 64","claim 1 134217792 64 64","claim 2 134217856 64 64","release 0 2","release 0 1","release 1 1","owns 1 2","configure 134217728 2048","release 1 2","configure 134217728 2048","claim 0 134217728 64 64"]
    rows=execute(scan_native,["ledger","normal"],commands)
    assert [r["status"] for r in rows]==[1,0,2,1,0,1,0,1,0,1,0,1,1,0]
    assert rows[-1]["ticket"]==3 and rows[2]["ticket"]==1
    for bad in ["claim 0 0 64 64","claim 3 134217728 64 64","claim 1 134217728 0 64","claim 1 134217728 64 3","configure 4294967295 1"]:
        result_in=execute(scan_native,["ledger","normal"],["configure 134217728 1024",bad])[-1]
        assert result_in["status"] in [0,2] and not result_in["occupied"]


def test_short_ticket_retires_and_stale_release_cannot_steal_new_claim(scan_native):
    commands=["configure 134217728 1024"]
    for ticket in range(1,256):commands += ["claim 0 134217728 64 64",f"release 0 {ticket}"]
    commands += ["configure 134217728 2048","claim 0 134217728 64 64"]
    rows=execute(scan_native,["ledger","short"],commands)
    assert rows[-1]["status"]==3 and rows[-1]["ticket"]==255 and not rows[-1]["occupied"]
