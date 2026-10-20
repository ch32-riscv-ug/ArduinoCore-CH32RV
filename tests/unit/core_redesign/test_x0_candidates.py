"""X0 input/progress equivalence, corrected 50Hz touch profile and C batch."""
import importlib.util
import json
from pathlib import Path
import random
import shutil
import subprocess

import pytest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location("x0_reference", HERE / "app_oracle.py")
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


def ticks(first, last, opportunity="step"):
    return [command for tick in range(first, last) for command in (f"time {tick}", opportunity)]


@pytest.fixture(scope="module")
def native_x0(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp06-x0") / "control"
    compiler = shutil.which("g++"); assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(HERE / "x0_driver.cpp"), "-o", str(executable),
    ], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, candidate, commands, domain=0, opted=False):
    result = subprocess.run([str(executable), candidate, str(domain), str(int(opted))],
                            input="\n".join(commands) + "\n", capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return [json.loads(row) for row in result.stdout.splitlines()]


def expected(commands, domain=0, opted=False):
    model = reference.ApplicationOracle(touch_period=20)
    rows = []
    for command in commands:
        if command.startswith("opportunity "):
            where, profile, context = map(int, command.split()[1:])
            allowed = context == 0 and (where == 0 or (profile != 0 and
                       (domain == 0 or (profile == 2 and opted))))
            row = model.apply("step") if allowed else model.observe()
        else: row = model.apply(command)
        rows.append(row)
    return rows, model


def compare(executable, candidate, commands, domain=0, opted=False):
    oracle, model = expected(commands, domain, opted)
    rows = run(executable, candidate, commands, domain, opted)
    assert len(rows) == len(oracle)
    for i, (actual, target) in enumerate(zip(rows, oracle)):
        assert actual == target, (candidate, i, commands[i], actual, target)
        assert actual["started"] - actual["reclaimed"] == actual["active"]
        assert actual["completed"] - actual["reclaimed"] == actual["terminal"]
    return rows, model


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("touch", [False, True])
def test_baseline_and_p1_touch_fifty_per_second(native_x0, candidate, touch):
    rows, model = compare(native_x0, candidate, [f"touch {int(touch)}", "attach", "key 1", "key 0", *ticks(0, 1000)])
    assert rows[-1]["confirmed"] == 5020 and rows[-1]["measure_done"] == 10
    assert rows[-1]["touch_done"] == (50 if touch else 0)
    assert rows[-1]["frames"] == 2
    assert rows[-1]["revision"] == rows[-1]["displayed_revision"]
    assert all(transaction["returned"] for transaction in model.ledger)


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("owner", [1, 2, 3])
def test_bus_failure_and_recovery(native_x0, candidate, owner):
    rows, model = compare(native_x0, candidate, ["touch 1", "attach", f"fault {owner}", *ticks(0, 500)])
    failures = [t for t in model.ledger if not t["success"]]
    assert len(failures) == 1 and failures[0]["owner"] == owner and failures[0]["returned"]
    assert rows[-1]["stale"] == 0 and rows[-1]["frames"] >= 1


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
def test_pause_input_full_detach_and_frozen_measurement(native_x0, candidate):
    commands = ["touch 1", "attach", "key 1", "key 1", "key 1", "key 0", "key -1", "step",
                "time 3", "key 1", "key 0", "step", "detach", "attach", "finish 1 1 1",
                *ticks(4, 100), "source 7000 1200", "fault 2", *ticks(100, 150),
                "key 0", "time 1000", "time 1001", "step", *ticks(1002, 1200)]
    rows, _ = compare(native_x0, candidate, commands)
    assert rows[-1]["rejected"] == 1 and rows[-1]["confirmed"] == 5080
    assert rows[-1]["corruptions"] == 0


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
def test_apply_coalescing_in_one_bounded_input_snapshot(native_x0, candidate):
    rows, _ = compare(native_x0, candidate, ["attach", "key 1", "key 0", "key 1", "key 0", "step", *ticks(0, 10)])
    assert rows[-1]["pd_started"] == rows[-1]["pd_reclaimed"] == 1
    assert rows[-1]["confirmed"] == 5040


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
@pytest.mark.parametrize("domain,opted", [(0, False), (1, False), (1, True)])
def test_profile_and_forbidden_context_opportunities(native_x0, candidate, domain, opted):
    commands = ["touch 1", "attach", "key 1", "key 0"]
    for context in [1, 2, 3]: commands.append(f"opportunity 0 2 {context}")
    # Keep the fake clock monotonic even when progress is suppressed.
    time = 0
    for where in [1, 2]:
        for profile in [0, 1, 2]:
            commands.extend(ticks(time, time + 15, f"opportunity {where} {profile} 0")); time += 15
    commands.extend(ticks(time, time + 200))
    compare(native_x0, candidate, commands, domain, opted)


@pytest.mark.parametrize("candidate", ["R", "A", "B", "C"])
def test_busy_and_corruption_observers_are_live(native_x0, candidate):
    rows = run(native_x0, candidate, ["step", "probe_busy", "time 2", "probe_busy"])
    assert all(row["owner"] == 1 and row["started"] == 1 for row in rows)
    assert rows[-1]["terminal"] == 1
    rows = run(native_x0, candidate, ["step", "corrupt", "time 1"])
    assert rows[-1]["corruptions"] == 1


@pytest.mark.parametrize("seed", range(10))
def test_same_generated_inputs_and_opportunities_for_all_candidates(native_x0, seed):
    rng = random.Random(seed); commands = ["touch 1"]; time = 0
    for _ in range(3000):
        kind = rng.randrange(11)
        if kind == 0: commands.append("attach")
        elif kind == 1: commands.append("detach")
        elif kind <= 3: commands.append(f"key {rng.choice([-1, 0, 1])}")
        elif kind == 4: time += rng.randrange(30); commands.append(f"time {time}")
        elif kind == 5: commands.append(f"fault {rng.randint(1, 3)}")
        elif kind == 6: commands.append(f"source {rng.randrange(5000, 11001, 20)} {rng.randrange(3001)}")
        elif kind == 7: commands.append(f"touch {rng.randrange(2)}")
        elif kind == 8: commands.append(f"opportunity {rng.randrange(3)} {rng.randrange(3)} {rng.randrange(4)}")
        else: commands.append("step")
    commands.extend(ticks(time + 1, time + 201))
    for candidate in ["R", "A", "B", "C"]: compare(native_x0, candidate, commands)


def test_static_batch_snapshot_repost_and_delivery_permissions(native_x0):
    result = subprocess.run([str(native_x0), "batch"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert not result.stdout and not result.stderr
