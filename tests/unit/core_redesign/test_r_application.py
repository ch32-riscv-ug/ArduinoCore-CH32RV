"""Integrated X0: serialized bus ownership, periodic work, and fault traces."""

import importlib.util
import json
from pathlib import Path
import random
import subprocess

import pytest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("p2_integrated_reference", HERE / "app_oracle.py")
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)

_native_spec = importlib.util.spec_from_file_location("p2_native_build", HERE / "native_build.py")
native_build = importlib.util.module_from_spec(_native_spec)
_native_spec.loader.exec_module(native_build)


@pytest.fixture(scope="session")
def native_control(tmp_path_factory):
    return native_build.build(tmp_path_factory)


def ticks(start, stop):
    return [command for t in range(start, stop) for command in (f"time {t}", "step")]


def run_native(executable, commands):
    result = subprocess.run([executable, "app"], input="\n".join(commands) + "\n",
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert not result.stderr, result.stderr
    return [json.loads(row) for row in result.stdout.splitlines()]


def compare(executable, commands):
    model = reference.ApplicationOracle()
    expected = [model.apply(command) for command in commands]
    observed = run_native(executable, commands)
    assert len(observed) == len(expected)
    for i, (actual, oracle) in enumerate(zip(observed, expected)):
        assert actual == oracle, (i, commands[i], actual, oracle)
        assert actual["started"] - actual["reclaimed"] == actual["active"]
        assert actual["completed"] - actual["reclaimed"] == actual["terminal"]
        assert actual["corruptions"] == 0
    return observed, model


@pytest.mark.parametrize("touch", [False, True], ids=["baseline", "touch-added"])
def test_shared_bus_and_periodic_sources(native_control, touch):
    commands = [f"touch {int(touch)}", "attach", "key 1", "key 0", *ticks(0, 1000)]
    rows, model = compare(native_control, commands)
    assert rows[-1]["confirmed"] == 5020
    assert rows[-1]["pd_started"] == rows[-1]["pd_reclaimed"] == 1
    assert rows[-1]["measure_done"] == 10
    assert rows[-1]["touch_done"] == (20 if touch else 0)
    assert rows[-1]["frames"] == 2  # Stable view does not request a frame every turn.
    assert rows[-1]["revision"] == rows[-1]["displayed_revision"]
    assert all(t["returned"] for t in model.ledger)


@pytest.mark.parametrize("owner", [1, 2, 3])
def test_failed_transaction_and_recovery(native_control, owner):
    commands = ["touch 1", "attach", f"fault {owner}", *ticks(0, 500)]
    rows, model = compare(native_control, commands)
    failures = [t for t in model.ledger if not t["success"]]
    assert len(failures) == 1 and failures[0]["owner"] == owner
    assert failures[0]["returned"]
    assert rows[-1]["stale"] == 0
    assert rows[-1]["frames"] >= 1


def test_hardware_progress_while_app_is_paused(native_control):
    commands = ["attach", "step", "time 1000", "time 1001", "step", *ticks(1002, 1200)]
    rows, _ = compare(native_control, commands)
    assert rows[3]["started"] == rows[3]["completed"] == 1
    assert rows[3]["reclaimed"] == 0 and rows[3]["terminal"] == 1
    assert rows[4]["missed_measure"] == 10
    assert rows[-1]["measure_done"] == 2  # Coalesced 1000ms acquisition, then 1100ms.


def test_measurement_failure_retains_value_and_marks_stale(native_control):
    commands = ["attach", *ticks(0, 99), "source 7000 1200", "fault 2", *ticks(99, 150),
                *ticks(150, 250)]
    rows, _ = compare(native_control, commands)
    stale = [row for row in rows if 105 <= row["time"] <= 150]
    assert stale and all(row["mv"] == 5000 and row["stale"] == 1 for row in stale)
    assert rows[-1]["mv"] == 7000 and rows[-1]["ma"] == 1200 and rows[-1]["stale"] == 0


def test_measurement_source_changes_do_not_mutate_borrowed_payload(native_control):
    commands = ["step", "time 2", "step", "source 7000 1200", "time 4", "step", *ticks(5, 110)]
    rows, _ = compare(native_control, commands)
    assert rows[2]["owner"] == 2
    assert rows[5]["mv"] == 5000
    assert rows[-1]["mv"] == 7000


def test_input_overflow_detach_and_stale_completion(native_control):
    commands = ["attach", "key 1", "key 1", "key 1", "key 0", "key -1", "step",
                "time 3", "key 1", "key 0", "step", "detach", "attach", "finish 1 1 1",
                *ticks(4, 20), "key 0", *ticks(20, 250)]
    rows, _ = compare(native_control, commands)
    assert rows[-1]["rejected"] == 1
    assert rows[-1]["pd_started"] == rows[-1]["pd_reclaimed"] == 2
    assert rows[-1]["confirmed"] == 5080


def test_busy_submission_cannot_replace_owner(native_control):
    rows = run_native(native_control, ["step", "probe_busy", "time 2", "probe_busy"])
    assert all(row["owner"] == 1 and row["started"] == 1 for row in rows)
    assert rows[-1]["terminal"] == 1  # Terminal still occupies the accepted slot.


def test_buffer_mutation_detector_is_live(native_control):
    rows = run_native(native_control, ["step", "corrupt", "time 1"])
    assert rows[-1]["corruptions"] == 1


@pytest.mark.parametrize("seed", range(10))
def test_randomized_application_interleavings(native_control, seed):
    rng = random.Random(seed)
    commands = ["touch 1"]
    time = 0
    for _ in range(3000):
        kind = rng.randrange(8)
        if kind == 0: commands.append("attach")
        elif kind == 1: commands.append("detach")
        elif kind == 2: commands.append(f"key {rng.choice([-1, 0, 1])}")
        elif kind == 3:
            time += rng.randrange(30)
            commands.append(f"time {time}")
        elif kind == 4: commands.append(f"fault {rng.randint(1, 3)}")
        elif kind == 5: commands.append(f"source {rng.randrange(5000, 11001, 20)} {rng.randrange(3001)}")
        else: commands.append("step")
    compare(native_control, commands)
