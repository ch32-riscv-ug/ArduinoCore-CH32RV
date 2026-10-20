"""P2 logic controls versus independent oracles; no MCU/peripheral access."""

import importlib.util
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess

import pytest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("p2_oracle", HERE / "oracle.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
TRACES = json.loads((HERE / "traces.json").read_text())["traces"]
MODELS = {"knob": oracle.KnobOracle, "pps": oracle.PpsOracle, "screen": oracle.ScreenOracle}


@pytest.fixture(scope="session")
def native_control(tmp_path_factory):
    compiler = shutil.which("g++")
    if not compiler:
        pytest.fail("P2 logic checks require an explicitly installed host g++")
    destination = tmp_path_factory.mktemp("p2-native") / "control"
    subprocess.run([compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer", str(HERE / "host_driver.cpp"), "-o", destination],
                   check=True, capture_output=True, text=True)
    return destination


def run_control(executable, target, commands):
    result = subprocess.run([executable, target], input="\n".join(commands) + "\n",
                            text=True, capture_output=True, env={**os.environ, "LC_ALL": "C"})
    assert result.returncode == 0, result.stderr
    assert not result.stderr, result.stderr
    return [list(map(int, row.split())) for row in result.stdout.splitlines()]


@pytest.mark.parametrize("trace", TRACES, ids=lambda trace: trace["id"])
def test_fixed_trace_matches_oracle(native_control, trace):
    model = MODELS[trace["target"]]()
    expected = [model.apply(line) for line in trace["commands"]]
    observed = run_control(native_control, trace["target"], trace["commands"])
    assert len(observed) == len(expected)
    for i, (actual, reference) in enumerate(zip(observed, expected)):
        assert actual == reference, (trace["id"], i, trace["commands"][i])


def test_named_requirements_have_literal_outcomes():
    results = {}
    for trace in TRACES:
        model = MODELS[trace["target"]]()
        rows = [model.apply(line) for line in trace["commands"]]
        results[trace["id"]] = rows
    assert results["K-NORMAL"][-1][:7] == [2, 1, -1000, 1000, 0, 0, 0]
    assert results["K-CONFIG"][-1][1:6] == [3, -1000, 1000, 2, 2]
    assert results["K-SATURATE"][-1][0:7] == [1000, 1, -1000, 1000, 0, 1, 0]
    assert results["K-WRAP"][-1][4] == 0
    assert results["X-CHANGE"][-1][2:6] == [5040, 1, 5040, 5040]
    assert results["X-CHANGE"][-1][9:11] == [2, 2]
    assert results["X-FULL"][-1][2] == 5060
    assert results["X-FULL"][-1][11:13] == [4, 1]
    assert results["X-DETACH-FIRST"][-1][3] == 0
    assert results["X-DETACH-FIRST"][-1][9:11] == [1, 1]
    assert results["X-DUPLICATE-RESULT"][-1][9:11] == [1, 1]
    assert results["X-STALE"][-1][-3:] == [5040, 1010, 0]
    assert results["X-DRAW"][-1][1:3] == [4, 4]
    assert results["X-DRAW"][-1][-1] == 2


@pytest.mark.parametrize("desired,confirmed,valid", [(5000, 0, 0), (5020, 5000, 1), (11000, 10000, 1), (5000, -1, 1)])
def test_entire_rendered_frame_matches_golden(native_control, desired, confirmed, valid):
    golden = json.loads((HERE / "frame-golden.json").read_text())
    entry = next(v for v in golden["frames"] if v["input"] == [desired, confirmed, valid])
    result = subprocess.run([native_control, "render"], input=f"view {desired} {confirmed} {valid}\n",
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    expected = bytes.fromhex(entry["hex"])
    assert len(expected) == 1024
    assert bytes.fromhex(result.stdout.strip()) == expected
    assert oracle.render_frame(desired, confirmed, 5000, 1000, bool(valid), False, True) == expected


def test_apply_queued_before_detach_cannot_restart_new_session(native_control):
    commands = ["attach", "key 0", "detach", "attach", "step"]
    rows = run_control(native_control, "pps", commands)
    assert rows[-1][9] == 0, "old session's queued Apply must not request power after reconnect"


@pytest.mark.parametrize("seed", range(10))
def test_knob_randomized_capacity_and_snapshots(native_control, seed):
    rng = random.Random(seed)
    commands = []
    for _ in range(10000):
        choice = rng.randrange(8)
        if choice == 0:
            commands.append(f"input {rng.randint(-32768, 32767)}")
        elif choice == 1:
            frame = struct.pack("<Hhhh", rng.randrange(65536), rng.randint(-2, 130),
                                rng.randint(-2000, 0), rng.randint(-1, 2000))
            commands += ["begin_write", *[f"byte {byte}" for byte in frame], "stop"]
        else:
            commands.append(("step", "read", "begin_read", "restart", "stop", "step")[choice - 2])
    model = oracle.KnobOracle()
    expected = [model.apply(line) for line in commands]
    assert run_control(native_control, "knob", commands) == expected


@pytest.mark.parametrize("seed", range(10))
def test_pps_randomized_session_and_completion(native_control, seed):
    rng = random.Random(seed)
    model = oracle.PpsOracle()
    commands, expected = [], []
    time = 0
    for _ in range(10000):
        choice = rng.randrange(8)
        if choice == 0:
            command = "attach" if not model.online else "detach"
        elif choice < 4:
            command = f"key {(-1, 0, 1)[choice - 1]}"
        elif choice == 4:
            time += rng.randrange(10)
            command = f"time {time}"
        elif choice == 5:
            command = f"finish {rng.randint(0, len(model.transactions) + 1)} {rng.randint(0, model.connection + 1)} {rng.randrange(2)}"
        else:
            command = "step"
        commands.append(command)
        expected.append(model.apply(command))
    assert run_control(native_control, "pps", commands) == expected
