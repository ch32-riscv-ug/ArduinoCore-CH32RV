"""Clock sequencer and owned free-running SysTick probes; no physical hardware."""
import importlib.util
from pathlib import Path
import random
import shutil
import subprocess
import pytest
HERE=Path(__file__).parent
spec=importlib.util.spec_from_file_location("clock_time_reference",HERE/"clock_time_oracle.py")
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)


@pytest.fixture(scope="module")
def native_clock_time(tmp_path_factory):
    exe=tmp_path_factory.mktemp("clock-time")/"control";compiler=shutil.which("g++");assert compiler
    r=subprocess.run([compiler,"-std=c++17","-O1","-g","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
        "-fno-sanitize-recover=all","-fno-omit-frame-pointer",str(HERE/"clock_time_driver.cpp"),"-o",str(exe)],capture_output=True,text=True,timeout=120)
    assert r.returncode==0,r.stderr
    return exe


def compare(exe,wide,commands):
    r=subprocess.run([str(exe),"X" if wide else "V"],input="\n".join(commands)+"\n",capture_output=True,text=True,timeout=60)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[list(map(int,line.split())) for line in r.stdout.splitlines()];assert len(rows)==len(commands)
    model=reference.Model(wide)
    for index,(line,row) in enumerate(zip(commands,rows)):
        target=model.apply(line);assert row==target,(wide,index,line,row,target)
    return rows


@pytest.mark.parametrize("wide",[False,True],ids=["V003","X035"])
@pytest.mark.parametrize("trim",[0,16,31,128,255])
def test_clock_readiness_and_factory_trim(native_clock_time,wide,trim):
    commands=[f"hardware 1 80 64 {trim}","clock_begin 1","clock_begin 1","clock_step","clock_step",f"hardware 3 80 64 {trim}","clock_step"]
    if wide:commands += ["clock_step"]
    else:commands += ["clock_step",f"hardware {0x3000003} 0 65 {trim}","clock_step","clock_step",f"hardware {0x3000003} 10 65 {trim}","clock_step"]
    commands += ["clock_step","clock_begin 1","clock_abandon"]
    rows=compare(native_clock_time,wide,commands);assert rows[-1][1]==4


@pytest.mark.parametrize("wide",[False,True])
def test_rejected_start_and_abandon_do_not_roll_back_hardware(native_clock_time,wide):
    rows=compare(native_clock_time,wide,["clock_begin 0","clock_abandon","hardware 50331649 10 64 255","clock_begin 1","clock_step","clock_abandon","clock_step","clock_begin 1"])
    assert rows[0][0]==2 and len(rows[0])==14
    if not wide:assert rows[3][0]==3 and rows[3][6:9]==[50331649,10,64]
    rows=compare(native_clock_time,wide,["clock_begin 1","clock_abandon","clock_step","clock_begin 1"])
    assert rows[-1][1]==5 and rows[-1][0]==1


@pytest.mark.parametrize("wide",[False,True])
@pytest.mark.parametrize("divide",[False,True])
def test_counter_initialization_epochs_wrap_and_no_timer_irq(native_clock_time,wide,divide):
    rows=compare(native_clock_time,wide,["sample 0 3","counter_begin 0 1 1","counter_begin 48000000 0 1",
        "counter_control 1",f"counter_begin 48000000 1 {int(divide)}","counter_control 0",
        f"counter_begin 48000000 1 {int(divide)}","counter_begin 48000000 1 0","ticks 4294967294 3","low 1","low 1",
        "stop","sample 1 3",f"counter_begin 48000000 1 {int(divide)}","low 1","low 2","sample 2 3"])
    assert rows[6][0]==0 and rows[6][9]==(1 if divide else 5) and not rows[6][9]&2
    assert rows[13][2]==2 and rows[14][0]==2 and len(rows[14])==14
    assert rows[16][0]==0


def test_wide_counter_retry_is_bounded_and_failure_keeps_output(native_clock_time):
    rows=compare(native_clock_time,True,["counter_begin 48000000 1 1","ticks 4294967295 1","sample 1 3",
        "ticks 4294967295 4294967296","sample 1 100","sample 1 0","low 1"])
    assert rows[2][0]==0 and rows[2][5]>0xffffffff
    assert rows[4][0]==3 and rows[4][5]==rows[2][5] and len(rows[4])==14+27
    assert rows[5][0]==3 and len(rows[5])==14


@pytest.mark.parametrize("wide",[False,True])
def test_duration_conversion_is_not_per_sample_time_extension(native_clock_time,wide):
    commands=[f"convert {ticks} {hz}" for ticks in [0,1,5,6,47,48,0xffffffff] for hz in [1,6000000,48000000,0xffffffff]]
    rows=compare(native_clock_time,wide,commands)
    assert rows[6][5]==0  # One tick need not produce a whole microsecond.


@pytest.mark.parametrize("seed",range(8))
def test_generated_backend_operations(native_clock_time,seed):
    rng=random.Random(seed);commands=[]
    for _ in range(2000):
        op=rng.randrange(12)
        if op==0:commands.append(f"hardware {rng.getrandbits(32)} {rng.getrandbits(20)} {rng.getrandbits(16)} {rng.randrange(256)}")
        elif op==1:commands.append(f"clock_begin {rng.randrange(2)}")
        elif op==2:commands.append("clock_step")
        elif op==3:commands.append("clock_abandon")
        elif op==4:commands.append(f"counter_begin {rng.choice([0,1,7,8,48000000,48000001])} {rng.randrange(2)} {rng.randrange(2)}")
        elif op==5:commands.append("stop")
        elif op==6:commands.append(f"ticks {rng.getrandbits(64)} {rng.choice([0,1,0xffffffff,0x100000000])}")
        elif op==7:commands.append(f"counter_control {rng.choice([0,1,2,5])}")
        elif op==8:commands.append(f"sample {rng.randrange(20)} {rng.randrange(6)}")
        elif op==9:commands.append(f"low {rng.randrange(20)}")
        else:commands.append(f"convert {rng.getrandbits(32)} {rng.choice([0,1,6000000,48000000])}")
    for wide in [False,True]:compare(native_clock_time,wide,commands)


@pytest.mark.parametrize("wide",[False,True])
def test_clock_confirmation_then_explicit_counter_epoch(native_clock_time,wide):
    commands=["hardware 3 0 0 255","clock_begin 1","clock_step"]
    if not wide:commands += ["hardware 50331651 0 1 255","clock_step","hardware 50331651 10 1 255"]
    commands += ["clock_step","counter_begin 48000000 1 1","ticks 6000000 0","sample 1 3","convert 6000000 6000000",
                 "stop","counter_begin 48000000 1 1","sample 1 3","sample 2 3"]
    rows=compare(native_clock_time,wide,commands)
    assert rows[-1][1]==4 and rows[-1][2]==2
    assert rows[-2][0]==2 and rows[-2][5]==1000000
    assert rows[-1][0]==0 and rows[-1][5]==0


@pytest.mark.parametrize("wide",[False,True])
def test_constant_and_specialized_duration_agree_for_full_u32_range(native_clock_time,wide):
    rng=random.Random(1701);values=[0,1,5,6,7,0xffffffff,*[rng.getrandbits(32) for _ in range(1000)]]
    commands=[f"{mode} {value}" for value in values for mode in ["convert_constant","convert_specialized"]]
    rows=compare(native_clock_time,wide,commands)
    for i in range(0,len(rows),2):assert rows[i][5]==rows[i+1][5]


@pytest.mark.parametrize("wide",[False,True])
def test_whole_u32_wrap_is_ambiguous_for_narrow_counter(native_clock_time,wide):
    rows=compare(native_clock_time,wide,["counter_begin 48000000 1 1","ticks 7 0","sample 1 3","ticks 4294967303 0","sample 1 3"])
    if wide:assert rows[4][5]-rows[2][5]==0x100000000
    else:assert rows[4]==rows[2]


def test_adding_truncated_small_durations_loses_fraction(native_clock_time):
    rows=compare(native_clock_time,False,[*["convert_specialized 1"]*6,"convert_specialized 6"])
    assert sum(r[5] for r in rows[:6])==0 and rows[6][5]==1
