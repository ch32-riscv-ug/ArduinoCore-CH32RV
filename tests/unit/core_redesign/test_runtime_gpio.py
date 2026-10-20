"""Register transaction checks for the experimental low-eight GPIO backend."""
from pathlib import Path
import random
import shutil
import subprocess
import pytest


@pytest.fixture(scope="module")
def native_gpio(tmp_path_factory):
    exe=tmp_path_factory.mktemp("runtime-gpio")/"control"
    compiler=shutil.which("g++");assert compiler
    r=subprocess.run([compiler,"-std=c++17","-O1","-g","-Wall","-Wextra","-Werror",
        "-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer",
        str(Path(__file__).with_name("runtime_gpio_driver.cpp")),"-o",str(exe)],capture_output=True,text=True,timeout=60)
    assert r.returncode==0,r.stderr
    return exe


def model(case):
    variant,x,pin,rcc,config,high=case
    if pin>=8:return [0,0,rcc,config]
    clock=0x40021018;port=0x40010800 if x else 0x40011400;mask=4 if x else 32
    nibble=4*pin;changed=(config&~(15<<nibble))|(1<<nibble)
    return [1,1,rcc|mask,changed,
            0,clock,rcc,1,clock,rcc|mask,1,port+16,1<<(pin+16),
            0,port,config,1,port,changed,1,port+16,1<<(pin+(0 if high else 16))]


def compare(exe,cases):
    r=subprocess.run([str(exe)],input="\n".join(" ".join(map(str,c)) for c in cases)+"\n",capture_output=True,text=True,timeout=30)
    assert r.returncode==0,r.stderr;assert not r.stderr
    rows=[list(map(int,line.split())) for line in r.stdout.splitlines()]
    assert len(rows)==len(cases)
    for c,row in zip(cases,rows):assert row==model(c),(c,row,model(c))


@pytest.mark.parametrize("variant",range(4))
@pytest.mark.parametrize("target",[0,1],ids=["V003","X035"])
def test_gpio_register_order_and_preserved_bits(native_gpio,variant,target):
    compare(native_gpio,[(variant,target,0,r,c,h) for r in [0,0xffffffff,0x13579bdf] for c in [0,0xffffffff,0x2468ace0] for h in [0,1]])


@pytest.mark.parametrize("seed",range(8))
def test_runtime_pin_boundaries_and_all_backend_forms(native_gpio,seed):
    rng=random.Random(seed)
    cases=[]
    for _ in range(1000):
        variant=rng.randrange(4)
        cases.append((variant,rng.randrange(2),0 if variant<2 else rng.choice([0,1,7,8,15,31,32,0xffffffff]),rng.getrandbits(32),rng.getrandbits(32),rng.randrange(2)))
    compare(native_gpio,cases)
