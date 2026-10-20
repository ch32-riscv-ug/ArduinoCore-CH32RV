"""Native sanitizer comparison with the independently implemented Python format model."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest

REPO=Path(__file__).resolve().parents[3]
UNIT=Path(__file__).parent


def module(name,file):
    spec=importlib.util.spec_from_file_location(name,file)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


media=module("store_media",UNIT/"flash_model.py")
formats=module("store_oracle",UNIT/"flash_formats.py")
inputs=json.loads((REPO/"planning/core-redesign/flash-format-inputs.json").read_text())
geometry=json.loads((REPO/"planning/core-redesign/flash-inputs.json").read_text())
CASES=[(t["part"],mode,g["program_unit"],g["erase_unit"],units,i,c)
       for t in geometry["targets"] for mode,g in t["modes"].items()
       for units in inputs["erase_units"] for i,c in enumerate(inputs["candidates"])]


def make(case,medium=None):
    m=medium if medium is not None else media.Medium(case[2],case[3],case[4]);c=case[6]
    return formats.Store(m,kind=c["kind"],policy=c["policy"],commit=c["commit"])


def case_id(case):return f"{case[0]}:{case[1]}:{case[4]}:{case[6]['id']}"
SUPPORTED=[case for case in CASES if make(case).layout()["supported"]]


def value(case,n):return {7:n*119,11:n*991} if case[6]["kind"]=="keys" else [b"",b"first-v1",b"old--v2!",b"next-v3!",b"last-v4!"][n]


def encode(value):
    return b"".join(struct.pack("<HI",k,value[k]) for k in sorted(value)) if isinstance(value,dict) else value


def begin_command(data,recover=False):
    payload=encode(data)
    return f"begin {len(payload)} {int(recover)} "+" ".join(map(str,payload))


@pytest.fixture(scope="module")
def native_store(tmp_path_factory):
    compiler=shutil.which("g++");assert compiler
    dest=tmp_path_factory.mktemp("flash-store")/"control"
    command=[compiler,"-std=c++17","-O0","-g1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
             "-fno-sanitize-recover=all","-fno-omit-frame-pointer",str(UNIT/"flash_store_driver.cpp"),"-o",str(dest)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=300)
    assert result.returncode==0,result.stderr
    return dest


def replay(exe,case,commands):
    result=subprocess.run([str(exe),str(case[5]),str(case[2]),str(case[3]),str(case[4])],input="\n".join(commands)+"\n",capture_output=True,text=True,timeout=120)
    assert result.returncode==0,result.stderr;assert not result.stderr
    rows=[json.loads(line) for line in result.stdout.splitlines()];assert len(rows)==len(commands)
    store=make(case);medium=store.medium;cut=None;answer=0xaabbccdd;old_image=None;old_hash=0
    statuses={"empty":0,"valid":1,"corrupt":2,"ambiguous":3,"unvalidated":4,"no-space":5}
    for command,row in zip(commands,rows):
        words=command.split();op=words[0];expected_status=99;events=[]
        try:
            if op=="fresh":store=make(case);medium=store.medium;cut=None
            elif op=="reset":store=make(case,medium);cut=None
            elif op=="abandon":store.abandon()
            elif op=="cut":cut=int(words[1])
            elif op=="poke":medium.data[int(words[1])]=int(words[2])
            elif op in ["begin","null_begin","put","remove"]:
                was_closed=store.closed;was_active=store.active is not None
                try:
                    if op=="begin":
                        size=int(words[1]);data=bytes(map(int,words[3:]));assert len(data)==size
                        if store.kind=="keys":
                            if len(data)>12 or len(data)%6:raise formats.Refused("invalid encoded schema")
                            pairs=list(struct.iter_unpack("<HI",data))
                            if any(a[0]>=b[0] for a,b in zip(pairs,pairs[1:])):raise formats.Refused("invalid encoded schema")
                            decoded=dict(pairs)
                        else:decoded=data
                        store.begin(decoded,recover_corrupt=bool(int(words[2])))
                    elif op=="null_begin":raise formats.Refused("null source")
                    else:store.change(int(words[1]),int(words[2]) if op=="put" else None)
                    expected_status=0
                except formats.Refused as error:
                    message=str(error)
                    if was_closed:expected_status=2
                    elif was_active:expected_status=1
                    elif "no space" in message or (op in ["put","remove"] and store.load()["status"]=="no-space"):expected_status=4
                    elif store.load()["status"]=="ambiguous":expected_status=6
                    elif "generation exhausted" in message:expected_status=7
                    elif "corrupt" in message or "not readable" in message:expected_status=5
                    else:expected_status=3
            elif op=="step":
                if store.closed:expected_status=4
                elif store.active is None:expected_status=0
                else:
                    operation,offset,data=store.operations[store.position]
                    length=medium.erase_unit if operation=="erase" else medium.program_unit
                    mask=None if cut is None else (bytes([255])*(length//2)+bytes(length-length//2) if cut==256 else bytes([cut])*length)
                    try:expected_status=2 if store.step(mask)=="done" else 1
                    except media.PowerCut:expected_status=3
                    events=[[0 if operation=="erase" else 1,offset,length,int(mask is None),list(data)]];cut=None
            elif op=="get":
                got=store.load();key=int(words[1]);expected_status=int(got["status"]=="valid" and key in got["value"])
                if expected_status:answer=got["value"][key]
            else:assert op in ["load","dump"]
        except Exception as error:raise AssertionError((case_id(case),command,error)) from error
        got=store.load();payload=[] if got["value"] is None else list(encode(got["value"]))
        if store.closed:phase=4
        elif store.active is None:phase=0
        elif store.operations[store.position][0]=="erase":phase=1
        else:
            operation=store.operations[store.position]
            footer_start=store.active["bank"]*store.bank_bytes+store.bank_bytes-store.footer_bytes
            phase=3 if store.commit and operation[1]>=footer_start else 2
        image=bytes(medium.data)
        if image!=old_image:
            old_hash=2166136261
            for byte in image:old_hash=((old_hash^byte)*16777619)&0xffffffff
            old_image=image
        expected=dict(status=expected_status,supported=int(store.layout()["supported"]),active=int(store.active is not None),phase=phase,
                      load=statuses[got["status"]],generation=got["generation"],bank=255 if got["bank"] is None else got["bank"],
                      size=len(payload),payload=payload,hash=old_hash,answer=answer,events=events,memory=list(image) if op=="dump" else [])
        for key,value_in in expected.items():assert row[key]==value_in,(case_id(case),command,key,row[key],value_in)
        assert len(events)<=1
    return rows


def append_update(commands,planner,data):
    commands.append(begin_command(data));planner.begin(data)
    while planner.active is not None:commands.append("step");planner.step()


@pytest.mark.parametrize("case",CASES,ids=case_id)
def test_native_normal_records_and_key_changes(native_store,case):
    planner=make(case);commands=["load","null_begin 8"]
    if not planner.layout()["supported"]:
        commands += [begin_command(value(case,1)),"step","dump"]
    else:
        for n in [1,2,3]:append_update(commands,planner,value(case,n))
        if planner.kind=="keys":
            for command,key,new in [("put 7 42",7,42),("remove 11",11,None),("remove 7",7,None)]:
                commands.append(command);planner.change(key,new)
                while planner.active is not None:commands.append("step");planner.step()
                commands += ["get 7","get 11"]
        commands += ["load","dump"]
    replay(native_store,case,commands)


@pytest.mark.parametrize("case",SUPPORTED,ids=case_id)
@pytest.mark.parametrize("initial",[True,False],ids=["first-write","replacement"])
def test_native_first_and_replacement_cut_boundaries(native_store,case,initial):
    seed=make(case)
    if not initial:
        # Establish both old versions with distinct generations.
        for n in [1,2]:
            seed.begin(value(case,n))
            while seed.active is not None:seed.step()
    seed.begin(value(case,3));count=len(seed.operations);commands=[]
    masks=[None,0,1,7,85,170,255,256]
    for completed in range(count+1):
        variants=[None] if completed==count else masks
        for cut in variants:
            planner=make(case);commands.append("fresh")
            if not initial:
                for n in [1,2]:append_update(commands,planner,value(case,n))
            commands.append(begin_command(value(case,3)));planner.begin(value(case,3))
            for _ in range(completed):commands.append("step");planner.step()
            if cut is None:commands.append("abandon");planner.abandon()
            else:
                commands += [f"cut {cut}","step"]
                operation=planner.operations[planner.position];length=planner.medium.erase_unit if operation[0]=="erase" else planner.medium.program_unit
                mask=bytes([255])*(length//2)+bytes(length-length//2) if cut==256 else bytes([cut])*length
                with pytest.raises(media.PowerCut):planner.step(mask)
            commands += ["reset","load"];planner=make(case,planner.medium);got=planner.load()
            if planner.kind!="raw" and got["status"]=="valid":
                assert got["value"] in ([value(case,3)] if initial else [value(case,2),value(case,3)])
            if not initial and planner.policy=="two-bank":assert got["status"]=="valid"
            recover=got["status"]=="corrupt";data=value(case,4)
            commands.append(begin_command(data,recover));planner.begin(data,recover_corrupt=recover)
            while planner.active is not None:commands.append("step");planner.step()
            commands.append("load")
    replay(native_store,case,commands)


def poke(offset,data):return [f"poke {offset+i} {byte}" for i,byte in enumerate(data)]


@pytest.mark.parametrize("commit",[False,True])
def test_native_generation_limit_and_ambiguous_banks(native_store,commit):
    case=next(c for c in SUPPORTED if c[2]==2 and c[4]==2 and c[6]["kind"]=="blob" and c[6]["policy"]=="two-bank" and c[6]["commit"]==commit)
    prefix=struct.pack("<4sIHH",b"BLB1",0xffffffff,8,1);payload=b"old--v2!"
    checksum=zlib.crc32(prefix+payload)&0xffffffff;frame=prefix+struct.pack("<I",checksum)+payload
    planner=make(case);commands=poke(0,frame)
    footer=b"END1"+struct.pack("<II",0xffffffff,checksum)
    if commit:commands+=poke(planner.bank_bytes-planner.footer_bytes,footer)
    commands += ["load",begin_command(b"next-v3!"),"step"]
    commands+=poke(planner.bank_bytes,frame)
    if commit:commands+=poke(2*planner.bank_bytes-planner.footer_bytes,footer)
    commands += ["load",begin_command(b"next-v3!",True),"dump"]
    replay(native_store,case,commands)


def test_native_corrupt_region_requires_explicit_replacement(native_store):
    case=next(c for c in SUPPORTED if c[2]==64 and c[4]==2 and c[6]["id"]=="blob-two-bank")
    planner=make(case);commands=["poke 0 0","load",begin_command(value(case,3)),"step",begin_command(value(case,3),True)]
    planner.medium.data[0]=0;planner.begin(value(case,3),recover_corrupt=True)
    while planner.active is not None:commands.append("step");planner.step()
    commands += ["load","abandon",begin_command(value(case,4)),"step"]
    replay(native_store,case,commands)


@pytest.mark.parametrize("commit",[False,True])
def test_native_key_limits_and_schema_refusals(native_store,commit):
    case=next(c for c in SUPPORTED if c[2]==64 and c[3]==64 and c[4]==4 and c[6]["kind"]=="keys" and c[6]["policy"]=="two-bank" and c[6]["commit"]==commit)
    planner=make(case);commands=[]
    bad=[b"x",struct.pack("<HIHI",7,1,7,2),struct.pack("<HIHI",11,1,7,2),struct.pack("<HIHIHI",1,1,2,2,3,3)]
    for data in bad:commands.append(begin_command(data))
    data={0:0xffffffff,65535:0xffffffff};append_update(commands,planner,data)
    commands += ["get 0","get 65535","get 1","put 1 42","load","dump"]
    replay(native_store,case,commands)
