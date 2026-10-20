"""Fixed absent/present touch, hand-stripped baseline and historical runtime flag."""
import importlib.util
import json
from pathlib import Path
import random
import shutil
import subprocess

import pytest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location("x0_feature_reference", HERE / "app_oracle.py")
reference = importlib.util.module_from_spec(spec); spec.loader.exec_module(reference)


def ticks(first, last, opportunity="step"):
    return [command for tick in range(first, last) for command in (f"time {tick}", opportunity)]


@pytest.fixture(scope="module")
def native_features(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp06-x0-features") / "control"
    compiler = shutil.which("g++"); assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(HERE / "x0_features_driver.cpp"), "-o", str(executable),
    ], capture_output=True, text=True, timeout=240)
    assert result.returncode == 0, result.stderr
    return executable


def enabled(mode):
    return mode in ["on", "runtime1"]


def run(executable, candidate, mode, commands, domain=0, opted=False):
    result = subprocess.run([str(executable), candidate, mode, str(domain), str(int(opted))],
                            input="\n".join(commands) + "\n", capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return [json.loads(row) for row in result.stdout.splitlines()]


def expected(commands, touch, domain=0, opted=False):
    model = reference.ApplicationOracle(touch_period=20); model.apply(f"touch {int(touch)}")
    rows = []
    for command in commands:
        if command.startswith("opportunity "):
            where, profile, context = map(int, command.split()[1:])
            allowed = context == 0 and (where == 0 or (profile != 0 and
                       (domain == 0 or (profile == 2 and opted))))
            row = model.apply("step") if allowed else model.observe()
        else: row = model.apply(command)
        rows.append(row)
    return rows


def compare(executable, candidate, mode, commands, domain=0, opted=False, target=None):
    if target is None: target = expected(commands, enabled(mode), domain, opted)
    actual = run(executable, candidate, mode, commands, domain, opted)
    assert len(actual) == len(target)
    for i, (row, correct) in enumerate(zip(actual, target)):
        assert row == correct, (candidate, mode, i, commands[i], row, correct)
        assert row["started"] - row["reclaimed"] == row["active"]
        assert row["completed"] - row["reclaimed"] == row["terminal"]
    return actual


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("mode", ["off", "on", "manual", "runtime0", "runtime1"])
def test_same_baseline_and_fifty_hertz_workload(native_features, candidate, mode):
    rows = compare(native_features, candidate, mode, ["attach", "key 1", "key 0", *ticks(0, 1000)])
    assert rows[-1]["confirmed"] == 5020 and rows[-1]["measure_done"] == 10
    assert rows[-1]["touch_done"] == (50 if enabled(mode) else 0)
    assert rows[-1]["frames"] == 2 and rows[-1]["revision"] == rows[-1]["displayed_revision"]


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("mode", ["off", "on", "manual"])
def test_faults_stall_input_sessions_and_fixed_feature(native_features, candidate, mode):
    commands = ["attach", "key 1", "key 0", "key 1", "key 0", "key 1", "step", "fault 1",
                *ticks(0, 50), "fault 2", "source 7000 1200", *ticks(50, 110),
                "detach", "attach", "finish 1 1 1", "key 0", "time 1000", "step", *ticks(1001, 1150)]
    if enabled(mode): commands += ["fault 3", *ticks(1150, 1250)]
    rows = compare(native_features, candidate, mode, commands)
    assert rows[-1]["rejected"] == 1 and rows[-1]["corruptions"] == 0


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("mode", ["off", "on", "manual"])
@pytest.mark.parametrize("domain,opted", [(0, False), (1, False), (1, True)])
def test_same_automatic_gates_and_forbidden_contexts(native_features, candidate, mode, domain, opted):
    commands = ["attach", "key 1", "key 0"]
    for context in [1, 2, 3]: commands.append(f"opportunity 0 2 {context}")
    time = 0
    for where in [1, 2]:
        for profile in [0, 1, 2]:
            commands.extend(ticks(time, time + 15, f"opportunity {where} {profile} 0")); time += 15
    commands.extend(ticks(time, time + 200))
    compare(native_features, candidate, mode, commands, domain, opted)


@pytest.mark.parametrize("seed", range(6))
def test_same_generated_trace_for_fixed_manual_and_runtime(native_features, seed):
    rng = random.Random(seed); commands = []; time = 0
    for _ in range(1000):
        kind = rng.randrange(10)
        if kind == 0: commands.append("attach")
        elif kind == 1: commands.append("detach")
        elif kind <= 3: commands.append(f"key {rng.choice([-1, 0, 1])}")
        elif kind == 4: time += rng.randrange(30); commands.append(f"time {time}")
        elif kind == 5: commands.append(f"fault {rng.randint(1, 2)}")
        elif kind == 6: commands.append(f"source {rng.randrange(5000, 11001, 20)} {rng.randrange(3001)}")
        elif kind == 7: commands.append(f"opportunity {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}")
        else: commands.append("step")
    commands.extend(ticks(time + 1, time + 201))
    targets = {touch: expected(commands, touch) for touch in [False, True]}
    for candidate in ["R", "A", "B", "C"]:
        for mode in ["off", "on", "manual", "runtime0", "runtime1"]:
            compare(native_features, candidate, mode, commands, target=targets[enabled(mode)])


def test_feature_cannot_be_enabled_by_runtime_input(native_features):
    for candidate in ["R", "A", "B", "C"]:
        result = subprocess.run([str(native_features), candidate, "off", "0", "0"], input="touch 1\n",
                                capture_output=True, text=True, timeout=60)
        assert result.returncode != 0 and "Assertion" in result.stderr


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("mode", ["off", "on", "manual"])
def test_bus_observers_are_live(native_features, candidate, mode):
    rows = run(native_features, candidate, mode, ["step", "probe_busy", "time 2", "probe_busy"])
    assert all(row["owner"] == 1 and row["started"] == 1 for row in rows)
    assert rows[-1]["terminal"] == 1
    rows = run(native_features, candidate, mode, ["step", "corrupt", "time 1"])
    assert rows[-1]["corruptions"] == 1


def test_absent_bus_owner_is_rejected_without_corrupting_active_io(native_features):
    result = subprocess.run([str(native_features), "contracts"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stdout and not result.stderr
