"""Protocol transcripts for a serial, non-DMA I2C master register engine."""
import json
from pathlib import Path
import random
import shutil
import subprocess
import pytest


@pytest.fixture(scope="module")
def native_i2c(tmp_path_factory):
    exe=tmp_path_factory.mktemp("i2c-master")/"control";compiler=shutil.which("g++");assert compiler
    r=subprocess.run([compiler,"-std=c++17","-O1","-g","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
        "-fno-sanitize-recover=all","-fno-omit-frame-pointer",str(Path(__file__).with_name("i2c_master_driver.cpp")),"-o",str(exe)],capture_output=True,text=True,timeout=120)
    assert r.returncode==0,r.stderr;return exe


def run(exe,commands,guarded=True,short=False):
    r=subprocess.run([str(exe),"guarded" if guarded else "raw","short" if short else "normal"],input="\n".join(commands)+"\n",capture_output=True,text=True,timeout=60)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[json.loads(line) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    for row in rows:
        assert row["confirmed"]<=row["issued"]<=64 and row["received"]<=64
        if row["pending"]:assert row["terminal"] and row["open"] and row["active"]
        if guarded:assert row["available"]==int(not row["active"])
        depth=0
        for kind,off,value in row["events"]:
            if kind==2:depth+=1
            elif kind==3:depth-=1
            assert depth in [0,1]
        assert depth==0
    return rows


def header():return ["open 1 48000000 400000"]
def address(read=False):return ["event 1 3","step","event 2 3","step"]
def transmit(n):
    return [command for _ in range(n) for command in ["event 128 3","step","step","event 132 3","step"]]
def receive(values):
    n=len(values)
    if n==1:return [f"push 1 {values[0]}","step"]
    commands=[f"push 2 {values[0]} {values[1]}"];position=2
    if n>2:
        for _ in range(n-3):
            commands += ["step",f"push 1 {values[position]}"];position+=1
        commands += ["step",f"push 1 {values[position]}"]
    return [*commands,"step"]
def finish():return ["quiet 0","step","peek","ack 1","take 1"]


@pytest.mark.parametrize("guarded",[False,True],ids=["raw","guarded"])
@pytest.mark.parametrize("length",[1,2,3,4,8,32,64])
def test_read_lengths_and_last_byte_protocol(native_i2c,guarded,length):
    payload=[(17+i*7)&255 for i in range(length)]
    commands=[*header(),f"start 0 {length} 60",*address(True),*receive(payload),*finish()]
    rows=run(native_i2c,commands,guarded)
    assert rows[-1]["out"]==[1,0,0,length,0] and rows[-1]["bytes"][:length]==payload
    assert rows[-1]["tx"]==[121] and not rows[-1]["active"]
    clear=rows[-1]["addr_ctl"][0]
    if length==1:assert clear&0xc00==0
    elif length==2:assert clear&0xc00==0xc00
    else:assert clear&0xc00==0x400
    addr_row=rows[5]
    if length==2:
        events=addr_row["events"];read_s2=next(i for i,e in enumerate(events) if e[:2]==[0,24]);clear_ack=next(i for i,e in enumerate(events) if e[0:2]==[1,0] and not e[2]&0x400)
        assert read_s2<clear_ack
    assert rows[-2]["active"] and not rows[-2]["pending"]


@pytest.mark.parametrize("guarded",[False,True])
@pytest.mark.parametrize("length",[1,2,8,32,64])
def test_btf_paced_write_and_no_early_buffer_return(native_i2c,guarded,length):
    data=[(11+i)&255 for i in range(length)]
    commands=[*[f"write {i} {v}" for i,v in enumerate(data)],*header(),f"start {length} 0 60",*address(),*transmit(length),"peek",*finish()]
    rows=run(native_i2c,commands,guarded)
    assert rows[-1]["out"]==[1,length,length,0,0] and rows[-1]["tx"]==[120,*data]
    before=rows[-6];assert before["active"] and not before["terminal"] and not before["pending"]
    assert rows[-1]["available"]


@pytest.mark.parametrize("guarded",[False,True])
@pytest.mark.parametrize("receive_length",[1,2,3,8,32])
def test_write_then_repeated_start_read_holds_one_loan(native_i2c,guarded,receive_length):
    values=[80+i for i in range(receive_length)]
    commands=["write 0 18","write 1 52",*header(),f"start 2 {receive_length} 72",*address(),*transmit(2),*address(True),*receive(values),*finish()]
    rows=run(native_i2c,commands,guarded)
    assert rows[-1]["tx"]==[144,18,52,145] and rows[-1]["bytes"][:receive_length+2]==[18,52,*values]
    assert rows[-1]["out"]==[1,2,2,receive_length,0]
    starts=[(i,e) for i,row in enumerate(rows) for e in row["events"] if e[:2]==[1,0] and e[2]&0x100]
    stop_rows=[i for i,row in enumerate(rows) for e in row["events"] if e[:2]==[1,0] and e[2]&0x200]
    assert len(starts)==2 and min(stop_rows)>starts[-1][0]


def test_open_validation_and_wire_busy_do_not_accept_a_request(native_i2c):
    rows=run(native_i2c,["open 0 48000000 400000","open 1 0 100000","open 1 48000001 100000","open 1 2000000 400000",
        "open 1 48000000 100001","open 1 48000000 100000","open 1 48000000 100000","event 0 2","start 1 0 60","event 0 0",
        "start 0 0 60","start 65 0 60","start 32 33 60","start 1 0 128","start 1 0 0","start 1 0 120","null_start 1 0 60","start 1 0 60"])
    assert [r["status"] for r in rows[:7]]==[3,2,2,2,2,0,1]
    assert rows[5]["regs"][1]==48 and rows[5]["regs"][7]==240
    assert rows[8]["status"]==1 and rows[8]["token"]==0 and rows[8]["available"]
    assert [rows[i]["status"] for i in range(10,18)]==[2,2,2,2,3,3,2,0]
    assert all(not rows[i]["active"] for i in range(10,17))


@pytest.mark.parametrize("guarded",[False,True])
def test_cancel_at_start_and_address_keeps_loan_until_local_stop(native_i2c,guarded):
    for prefix in [[],["event 1 3","step"],["event 1 3","step","event 2 3"]]:
        rows=run(native_i2c,[*header(),"start 0 8 60",*prefix,"cancel 1","take 1","step","step",*finish()],guarded)
        assert rows[-1]["out"]==[3,0,0,0,0]
        assert rows[-7]["active"] and not rows[-7]["terminal"]


def test_cancel_write_waits_for_inflight_btf_and_reports_known_prefix(native_i2c):
    rows=run(native_i2c,[*header(),"start 3 0 60",*address(),"event 128 3","step","cancel 1","step","take 1","event 132 3","step",*finish()])
    assert rows[-1]["out"]==[3,1,1,0,0]
    assert rows[-8]["active"] and not rows[-8]["terminal"]


def test_cancel_read_after_address_can_lose_to_full_success(native_i2c):
    rows=run(native_i2c,[*header(),"start 0 3 60",*address(True),"cancel 1",*receive([1,2,3]),*finish()])
    assert rows[-1]["out"]==[1,0,0,3,0] and rows[-1]["cancel"]


@pytest.mark.parametrize("flag",[0x100,0x200,0x400,0x800,0x1000])
def test_error_flags_retire_once_and_arbitration_loss_never_requests_stop(native_i2c,flag):
    commands=[*header(),"start 2 0 60",*address(),"event 128 3","step",f"event {flag} {2 if flag==0x200 else 3}","step","take 1","quiet 1","step","peek","take 1","take 1"]
    rows=run(native_i2c,commands)
    assert rows[-2]["out"]==[2,1,0,0,flag] and rows[-1]["out"]==rows[-2]["out"] and rows[-1]["status"]==0
    assert rows[-2]["regs"][6]&2 # BUSY may belong to another master.
    if flag==0x200:
        for row in rows[8:]:assert not any(e[:2]==[1,0] and e[2]&0x200 for e in row["events"])


def test_end_revokes_notice_without_releasing_active_buffer(native_i2c):
    rows=run(native_i2c,[*header(),"start 0 2 60",*address(True),"end","open 1 48000000 400000","write 0 99",*receive([3,4]),"quiet 0","step","peek","ack 1","take 1","open 1 48000000 400000"])
    assert rows[6]["active"] and not rows[6]["open"] and rows[6]["status"]==1
    assert rows[7]["active"] and rows[7]["status"]==1 # Reopen waits for collection.
    assert rows[8]["status"]==0 # User cannot modify the borrowed buffer.
    assert rows[-5]["terminal"] and not rows[-5]["pending"] and not rows[-5]["open"]
    assert rows[-2]["out"]==[1,0,0,2,0] and rows[-1]["status"]==0


@pytest.mark.parametrize("guarded",[False,True])
def test_user_access_policy_and_stale_tokens(native_i2c,guarded):
    rows=run(native_i2c,["write 0 17",*header(),"start 1 0 60","write 0 33","read 0","cancel 0","take 0",*address(),*transmit(1),*finish(),"start 0 1 60","cancel 1","take 1","cancel 2"],guarded)
    assert rows[3]["status"]==int(not guarded) and rows[4]["status"]==int(not guarded)
    assert rows[5]["status"]==3 and rows[6]["out"]==[2,77,78,79,80]
    assert rows[-3]["status"]==3 and rows[-2]["status"]==0 and rows[-1]["status"]==0
    assert rows[-4]["token"]==2


def test_u8_generation_retires_instead_of_reusing_token(native_i2c):
    commands=header()
    for token in range(1,256):commands += ["start 1 0 60",*address(),*transmit(1),"quiet 0","step",f"take {token}"]
    commands += ["start 1 0 60","end","open 1 48000000 400000","start 1 0 60"]
    rows=run(native_i2c,commands,short=True)
    assert rows[-4]["status"]==5 and rows[-1]["status"]==5 and rows[-1]["token"]==255 and rows[-1]["available"]


def test_partial_receive_error_drains_without_publishing_unreceived_tail(native_i2c):
    commands=[*header(),"start 0 8 60",*address(True),"push 2 11 22","step","push 1 33",
              "event 320 3","step","take 1","quiet 0","step","peek","step","peek","take 1","take 1"]
    rows=run(native_i2c,commands)
    assert rows[11]["active"] and not rows[11]["terminal"]
    assert rows[-2]["out"]==[2,0,0,1,256]
    assert rows[-2]["bytes"][:4]==[11,0,0,0]
    assert rows[-3]["notice"]==1 and rows[-3]["pending"]
    assert rows[-1]["status"]==0 and rows[-1]["out"]==rows[-2]["out"]
    drains=[e[2] for row in rows[12:] for e in row["events"] if e[:2]==[0,16]]
    assert drains==[22,33]


def test_stale_errors_cleared_before_accepting_new_generation(native_i2c):
    rows=run(native_i2c,[*header(),"event 1280 0","start 1 0 60",*address(),*transmit(1),*finish(),
                         "event 4 0","start 1 0 60","quiet 0","start 0 1 60"])
    assert rows[2]["status"]==0 and not rows[2]["regs"][5]&0x1f00
    assert rows[-3]["status"]==1 and rows[-3]["token"]==1
    assert rows[-1]["status"]==0 and rows[-1]["token"]==2


def test_cancel_before_repeated_start_address_does_not_begin_receive(native_i2c):
    rows=run(native_i2c,[*header(),"start 1 2 60",*address(),*transmit(1),"cancel 1","step",*finish()])
    assert rows[-1]["out"]==[3,1,1,0,0] and rows[-1]["tx"]==[120,0]


@pytest.mark.parametrize("seed",range(8))
def test_generated_valid_peer_transactions_have_same_payload_and_completion(native_i2c,seed):
    rng=random.Random(0x13200+seed);commands=header();expected=[];checks=[]
    for token in range(1,13):
        tx=rng.randint(0,12);rx=rng.randint(0,12)
        if tx+rx==0:rx=1
        target=rng.randint(8,119);outgoing=[rng.randrange(256) for _ in range(tx)];incoming=[rng.randrange(256) for _ in range(rx)]
        commands += [f"write {i} {value}" for i,value in enumerate(outgoing)]
        commands += [f"start {tx} {rx} {target}",*(["step"]*rng.randint(0,3)),*address()]
        if tx:
            commands += transmit(tx)
            if rx:commands += address(True)
        if rx:commands += receive(incoming)
        commands += ["quiet 0","step","peek","step","peek",f"ack {token}",f"take {token}"]
        checks.append((len(commands)-1,tx,rx,outgoing,incoming))
        expected += ([target*2,*outgoing] if tx else [])+([target*2+1] if rx else [])
    raw=run(native_i2c,commands,False);guarded=run(native_i2c,commands,True)
    for index,tx,rx,outgoing,incoming in checks:
        for rows in [raw,guarded]:
            assert rows[index]["out"]==[1,tx,tx,rx,0]
            assert rows[index]["bytes"][:tx+rx]==outgoing+incoming
            assert rows[index-2]["pending"] and rows[index-3]["pending"]
        assert raw[index]["tx"]==guarded[index]["tx"]
    assert guarded[-1]["tx"]==expected
