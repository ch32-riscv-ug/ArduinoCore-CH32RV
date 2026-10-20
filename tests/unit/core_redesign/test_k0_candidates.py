"""Same K0 frontend/meaning, four progress variants and capacity-two change."""
from collections import deque
import importlib.util
import json
from pathlib import Path
import random
import shutil
import struct
import subprocess

import pytest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location("k0_reference", HERE / "oracle.py")
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


class CapacityTwo:
    """Reuse frozen meaning; add a separate FIFO of completed transactions."""
    def __init__(self):
        self.core = reference.KnobOracle()
        self.configs = deque()

    def apply(self, command):
        c = self.core
        if command == "begin_write":
            c.status |= 4 if c.receiving is not None else 0
            c.snapshot = None
            c.accepted = int(len(self.configs) < 2)
            if not c.accepted: c.status |= 2
            c.receiving = [] if c.accepted else None
        elif command == "stop":
            if c.receiving is not None:
                if len(c.receiving) == 8: self.configs.append(struct.unpack("<Hhhh", bytes(c.receiving)))
                else: c.status |= 4
            c.receiving = c.snapshot = None
        elif command == "step":
            c.config = self.configs.popleft() if self.configs else None
            c.apply(command)
        else: c.apply(command)
        row = c.apply("observe")
        row[7] = len(self.configs)
        return row


def expected(commands, capacity, domain=0, opted=False):
    model = reference.KnobOracle() if capacity == 1 else CapacityTwo()
    initialized = False; status = 0; rows = []
    for command in commands:
        if command == "begin": initialized = True; row = model.apply("observe")
        elif command == "step" or command.startswith("opportunity "):
            where, profile, context = (0, 0, 0) if command == "step" else map(int, command.split()[1:])
            if not initialized: status = 1
            elif context in [1, 3]: status = 2
            elif context == 2: status = 3
            elif where != 0 and profile == 0: status = 4
            else: status = 0
            permitted = initialized and context == 0 and (where == 0 or
                         (profile != 0 and (domain == 0 or (profile == 2 and opted))))
            row = model.apply("step" if permitted else "observe")
        else: row = model.apply(command)
        rows.append([*row, status])
    return rows


@pytest.fixture(scope="module")
def native_k0(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp06-k0") / "control"
    compiler = shutil.which("g++"); assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(HERE / "k0_driver.cpp"), "-o", str(executable),
    ], capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, candidate, commands, capacity=1, domain=0, opted=False):
    result = subprocess.run([str(executable), candidate, str(capacity), str(domain), str(int(opted))],
                            input="\n".join(commands) + "\n", capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return [list(map(int, row.split())) for row in result.stdout.splitlines()]


TRACES = [trace for trace in json.loads((HERE / "traces.json").read_text())["traces"] if trace["target"] == "knob"]


@pytest.mark.parametrize("trace", TRACES, ids=lambda trace: trace["id"])
@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
def test_frozen_k0_contract(native_k0, trace, candidate):
    commands = ["begin", *trace["commands"]]
    assert run(native_k0, candidate, commands) == expected(commands, 1)


def frame(sequence, increment):
    return ["begin_write", *(f"byte {b}" for b in struct.pack("<Hhhh", sequence, increment, -1000, 1000)), "stop"]


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("capacity", [1, 2])
def test_two_config_change_preserves_capacity_order_and_retry(native_k0, candidate, capacity):
    commands = ["begin", *frame(1, 2), *frame(2, 3), *frame(3, 4), "input 2", "step", "step",
                *frame(3, 4), "input 1", "step", "begin_read", *("read" for _ in range(8))]
    rows = run(native_k0, candidate, commands, capacity)
    assert rows == expected(commands, capacity)
    assert rows[-1][:5] == [8, 4, -1000, 1000, 3]
    assert rows[-1][5] & 2


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("domain,opted", [(0, False), (1, False), (1, True)])
def test_same_profile_and_context_opportunities(native_k0, candidate, domain, opted):
    commands = ["opportunity 0 0 0", "begin", "input 3"]
    for context in [1, 2, 3]: commands.append(f"opportunity 0 2 {context}")
    for where in [1, 2]:
        for profile in [0, 1, 2]:
            commands.extend(["input 1", f"opportunity {where} {profile} 0"])
    commands.extend(["step", "begin_read", *("read" for _ in range(8))])
    assert run(native_k0, candidate, commands, domain=domain, opted=opted) == expected(commands, 1, domain, opted)


@pytest.mark.parametrize("capacity", [1, 2])
@pytest.mark.parametrize("seed", range(5))
def test_all_candidates_against_same_trace_and_oracle(native_k0, capacity, seed):
    rng = random.Random(seed); commands = ["begin"]
    for _ in range(5000):
        op = rng.randrange(10)
        if op < 2: commands.append(f"input {rng.randint(-32768, 32767)}")
        elif op == 2: commands.extend(frame(rng.randrange(65536), rng.choice([0, 1, 2, 127, 128])))
        elif op == 3: commands.append(f"byte {rng.randrange(256)}")
        elif op == 4: commands.append("begin_write")
        elif op == 5: commands.append("begin_read")
        elif op == 6: commands.append("read")
        elif op == 7: commands.append(rng.choice(["stop", "restart"]))
        elif op == 8: commands.append("step")
        else: commands.append(f"opportunity {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}")
    commands.extend(["step"] * capacity)
    commands.extend(["begin_read", *("read" for _ in range(10))])
    oracle = expected(commands, capacity)
    for candidate in ["R", "A", "B", "C"]:
        assert run(native_k0, candidate, commands, capacity) == oracle
