"""EXP-04 conditional format comparison against explicit payload expectations."""
import copy
import importlib.util
import json
from pathlib import Path
import struct

import pytest

REPO = Path(__file__).resolve().parents[3]


def load_module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    result = importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


media = load_module("exp04_media", Path(__file__).with_name("flash_model.py"))
formats = load_module("exp04_formats", Path(__file__).with_name("flash_formats.py"))
INPUT = json.loads((REPO/"planning/core-redesign/flash-format-inputs.json").read_text())
GEOMETRY = json.loads((REPO/"planning/core-redesign/flash-inputs.json").read_text())
CASES = [(t["part"], mode, g["program_unit"], g["erase_unit"], count, c)
         for t in GEOMETRY["targets"] for mode, g in t["modes"].items()
         for count in INPUT["erase_units"] for c in INPUT["candidates"]]


def make(case, medium=None):
    part, mode, program, erase, count, candidate = case
    m = medium if medium is not None else media.Medium(program, erase, count)
    return formats.Store(m, kind=candidate["kind"],policy=candidate["policy"],commit=candidate["commit"],
                         blob_size=INPUT["blob_bytes"],max_keys=INPUT["max_numeric_keys"])


def id_for(case):return ":".join([case[0],case[1],str(case[4]),case[5]["id"]])
SUPPORTED = [case for case in CASES if make(case).layout()["supported"]]


def value(store, generation):
    if store.kind == "keys":return {7: generation * 119, 11: generation * 991}
    return {1:b"first-v1",2:b"old--v2!",3:b"next-v3!",4:b"last-v4!"}[generation]


def finish(store):
    while store.active is not None:
        before=len(store.medium.operations)
        result=store.step()
        assert len(store.medium.operations)==before+1
        assert result == ("done" if store.active is None else "pending")


def crc_reference(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):crc=(crc>>1) ^ (0xedb88320 if crc&1 else 0)
    return crc ^ 0xffffffff


def reference_frame(kind, generation, payload):
    prefix=struct.pack("<4sIHH",b"KEY1" if kind=="keys" else b"BLB1",generation,len(payload),1)
    return prefix+struct.pack("<I",crc_reference(prefix+payload))+payload


@pytest.mark.parametrize("case", CASES, ids=id_for)
def test_capacity_normal_updates_and_spec_bytes(case):
    s=make(case); layout=s.layout(); m=s.medium
    if not layout["supported"]:
        before=(bytes(m.data),list(m.attempted),list(m.operations))
        assert s.load()["status"]=="no-space"
        with pytest.raises(formats.Refused):s.begin(value(s,1))
        assert before==(bytes(m.data),m.attempted,m.operations)
        return
    for generation in [1,2,3]:
        s.begin(value(s,generation));finish(s);got=s.load()
        assert got["value"]==value(s,generation)
        assert got["status"]==("unvalidated" if s.kind=="raw" else "valid")
        assert got["generation"]==(0 if s.kind=="raw" else generation)
        start=got["bank"]*s.bank_bytes
        payload=struct.pack("<HIHI",7,generation*119,11,generation*991) if s.kind=="keys" else value(s,generation)
        frame=payload if s.kind=="raw" else reference_frame(s.kind,generation,payload)
        assert m.read(start,len(frame))==frame
        if s.commit:
            footer=b"END1"+struct.pack("<I",generation)+frame[12:16]
            assert m.read(start+s.bank_bytes-s.footer_bytes,12)==footer
    # Serialization uses a sorted numeric map, independently checked above.
    if s.kind=="keys":
        s.change(7,42);finish(s);assert s.load()["value"]=={7:42,11:2973}
        s.change(11,None);finish(s);assert s.load()["value"]=={7:42}
        s.change(7,None);finish(s);assert s.load()["value"]=={}


@pytest.mark.parametrize("case", SUPPORTED, ids=id_for)
def test_every_operation_boundary_and_selected_intra_operation_cut(case):
    seed=make(case)
    for generation in [1,2]:seed.begin(value(seed,generation));finish(seed)
    seed.begin(value(seed,3)); operations=list(seed.operations)
    previous=value(seed,2);new=value(seed,3)
    for completed in range(len(operations)+1):
        variants=[None] if completed==len(operations) else [None,*INPUT["faults"]["masks"],"prefix"]
        for mask_kind in variants:
            m=copy.deepcopy(seed.medium);writer=make(case,m);writer.begin(new)
            for _ in range(completed):writer.step()
            if mask_kind is None:writer.abandon()
            else:
                operation=writer.operations[writer.position]
                length=m.erase_unit if operation[0]=="erase" else m.program_unit
                mask=bytes([255])*(length//2)+bytes(length-length//2) if mask_kind=="prefix" else bytes([mask_kind])*length
                with pytest.raises(media.PowerCut):writer.step(mask)
                with pytest.raises(formats.Refused):writer.begin(new)
            recovered=make(case,m); got=recovered.load()
            if seed.policy=="two-bank":
                assert got["status"]=="valid" and got["value"] in [previous,new]
            elif seed.kind=="raw":
                assert got["status"]=="unvalidated"
            elif got["status"]=="valid":assert got["value"] in [previous,new]
            else:assert got["status"] in ["empty","corrupt"]
            if completed==len(operations):assert got["value"]==new
            if got["status"] in ["valid","empty","unvalidated"]:
                recovered.begin(value(recovered,4));finish(recovered)
                assert recovered.load()["value"]==value(recovered,4)
            else:
                before=(bytes(m.data),list(m.operations))
                with pytest.raises(formats.Refused):recovered.begin(value(recovered,4))
                assert before==(bytes(m.data),m.operations)
                recovered.begin(value(recovered,4),recover_corrupt=True);finish(recovered)
                assert recovered.load()["value"]==value(recovered,4)


def test_reference_crc_known_vector_and_fixed_frame():
    assert crc_reference(b"123456789")==0xcbf43926
    s=make(SUPPORTED[0]);s.begin(value(s,1));finish(s)
    # The first supported case is RAW; it never reports a validated value.
    assert s.kind=="raw" and s.load()["status"]=="unvalidated"


@pytest.mark.parametrize("commit",[False,True])
def test_generation_exhaustion_and_equal_generation_ambiguity(commit):
    m=media.Medium(2,1024,2);s=formats.Store(m,policy="two-bank",commit=commit)
    frame=reference_frame("blob",0xffffffff,b"old--v2!")
    m.data[:len(frame)]=frame
    if commit:m.data[1024-s.footer_bytes:1024-s.footer_bytes+12]=b"END1"+struct.pack("<I",0xffffffff)+frame[12:16]
    assert s.load()["generation"]==0xffffffff
    before=(bytes(m.data),list(m.operations))
    with pytest.raises(formats.Refused):s.begin(b"next-v3!")
    assert before==(bytes(m.data),m.operations)
    m.data[1024:1024+len(frame)]=frame
    if commit:m.data[2048-s.footer_bytes:2048-s.footer_bytes+12]=b"END1"+struct.pack("<I",0xffffffff)+frame[12:16]
    assert s.load()["status"]=="ambiguous"
    with pytest.raises(formats.Refused):s.begin(b"next-v3!",recover_corrupt=True)


@pytest.mark.parametrize("kind",["blob","keys"])
def test_invalid_format_is_not_silently_erased(kind):
    m=media.Medium(64,64,2);s=formats.Store(m,kind=kind)
    m.data[0]=0
    before=(bytes(m.data),list(m.operations))
    assert s.load()["status"]=="corrupt"
    with pytest.raises(formats.Refused):s.begin(value(s,1))
    with pytest.raises(formats.Refused):s.begin(value(s,1),recover_corrupt="yes")
    assert before==(bytes(m.data),m.operations)
    s.begin(value(s,1),recover_corrupt=True);finish(s);assert s.load()["value"]==value(s,1)


@pytest.mark.parametrize("payload",[struct.pack("<HIHI",7,1,7,2),struct.pack("<HIHI",11,1,7,2),b"x",struct.pack("<HIHIHI",1,1,2,2,3,3)])
def test_crc_valid_but_invalid_key_schema_is_rejected(payload):
    m=media.Medium(64,64,1);s=formats.Store(m,kind="keys")
    frame=reference_frame("keys",1,payload);m.data[:len(frame)]=frame
    assert s.load()["status"]=="corrupt"


def test_rejected_input_and_busy_updates_do_not_change_media():
    m=media.Medium(64,64,2);s=formats.Store(m,kind="keys")
    for value_in in [{True:3},{-1:3},{65536:3},{1:-1},{1:2**32},{1:True},{1:1,2:2,3:3},b"bad"]:
        with pytest.raises(formats.Refused):s.begin(value_in)
        assert not m.operations and all(v==255 for v in m.data)
    s.begin({1:10});before=list(s.operations)
    with pytest.raises(formats.Refused):s.begin({1:20})
    with pytest.raises(formats.Refused):s.change(2,20)
    assert s.operations==before and not m.operations
    finish(s)
    with pytest.raises(formats.Refused):s.step()
    s.abandon()
    with pytest.raises(formats.Refused):s.begin({1:30})


@pytest.mark.parametrize("kind",["raw","blob","keys"])
def test_overwrite_has_a_concrete_old_value_loss_window(kind):
    s=formats.Store(media.Medium(64,64,1),kind=kind)
    s.begin(value(s,2));finish(s);s.begin(value(s,3))
    assert s.step()=="pending" # The only erase completes before any program.
    s.abandon();got=formats.Store(s.medium,kind=kind).load()
    if kind=="raw":
        assert got["status"]=="unvalidated" and got["value"]==bytes([255])*8
    else:assert got["status"]=="empty" and got["value"] is None


def test_input_key_map_is_snapshotted_before_explicit_progress():
    s=formats.Store(media.Medium(64,64,2),kind="keys")
    supplied={7:1,11:2};s.begin(supplied);supplied[7]=999;supplied.clear()
    finish(s);assert s.load()["value"]=={7:1,11:2}
