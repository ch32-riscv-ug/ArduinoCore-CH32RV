"""IO-01..04 with a lease ledger, deterministic races and generated traces."""
import importlib.util
from pathlib import Path
import random
import shutil
import subprocess

import pytest

spec = importlib.util.spec_from_file_location("io_ledger", Path(__file__).with_name("io_oracle.py"))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


@pytest.fixture(scope="module")
def native_io(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp01-io") / "control"
    compiler = shutil.which("g++")
    assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("io_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, commands):
    result = subprocess.run([str(executable)], input="\n".join(commands) + "\n",
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return [list(map(int, row.split())) for row in result.stdout.splitlines()]


def check(executable, commands):
    model = oracle.IoLedger()
    expected = [model.apply(command) for command in commands]
    rows = run(executable, commands)
    assert rows == expected
    return rows


def test_start_status_and_rejected_requests_have_no_lease(native_io):
    commands = ["start 0 0 8 0 64", "open 0", "open 0", "start 0 -1 8 0 64",
                "start 0 0 0 0 64", "start 0 0 33 0 64", "start 0 0 8 0 128",
                "start 0 0 8 2 64", "start 0 0 8 0 64", "start 0 1 8 0 64",
                "open 1", "start 1 0 8 1 64", "start 1 1 8 1 64"]
    rows = check(native_io, commands)
    assert [row[0] for row in rows] == [4, 1, 0, 2, 2, 2, 2, 3, 0, 1, 1, 1, 0]
    assert [row[11] for row in rows[:8]] == [0] * 8


@pytest.mark.parametrize("outcome", [1, 2, 3])
@pytest.mark.parametrize("cancel_first", [False, True])
def test_terminal_races_and_exactly_one_collection(native_io, outcome, cancel_first):
    finish = f"finish 0 1 {outcome} 8 250"
    events = ["cancel 0 1", finish] if cancel_first else [finish, "cancel 0 1"]
    rows = check(native_io, ["open 0", "start 0 0 8 1 64", *events,
                            "finish 0 1 1 8 5", "finish 0 1 2 0 0", "peek 0", "peek 0",
                            "ack 0 1", "peek 0", "write 0 0 99", "take 0 1", "take 0 1"])
    assert rows[-2][0] == 1 and rows[-1][0] == 0
    # Cancelled needs a prior stop request. Otherwise the later success wins.
    expected = outcome if outcome != 3 or cancel_first else 1
    assert rows[-1][3:5] == [expected, 8]
    assert rows[-3][0] == 0  # Even terminal/notification-acked storage is held.


def test_cancel_acceptance_neither_stops_hardware_nor_releases_storage(native_io):
    commands = ["open 0", "start 0 0 8 1 64", "cancel 0 1", "cancel 0 1", "take 0 1",
                "write 0 0 99", "read 0 0", "open 1", "start 1 0 8 0 64",
                "finish 0 1 3 3 250", "read 0 0", "start 1 0 8 0 64", "take 0 1",
                "read 0 0", "read 0 2", "read 0 3", "start 1 0 8 0 64"]
    rows = check(native_io, commands)
    assert rows[2][0] == 0 and rows[3][0] == 1
    assert rows[4][0] == rows[5][0] == rows[6][0] == rows[10][0] == 0
    assert rows[11][0] == 1  # Busy until explicit collection.
    assert [rows[i][1] for i in [13, 14, 15]] == [250, 252, 0]


@pytest.mark.parametrize("finish_before_end", [False, True])
def test_end_revokes_delivery_and_old_tokens_do_not_reach_reopened_owner(native_io, finish_before_end):
    events = ["finish 0 1 1 8 7", "end 0"] if finish_before_end else ["end 0", "finish 0 1 1 8 7"]
    rows = check(native_io, ["open 0", "start 0 0 8 1 64", *events, "peek 0", "ack 0 1",
                            "open 0", "write 0 0 99", "take 0 1", "open 0", "start 0 0 8 1 64",
                            "finish 0 1 2 0 0", "cancel 0 1", "take 0 1", "finish 0 2 1 8 90",
                            "ack 0 1", "peek 0", "take 0 2", "read 0 0"])
    assert rows[4][0] == rows[5][0] == rows[6][0] == rows[7][0] == 0
    assert rows[-3][0:3:2] == [1, 2]  # Only the new generation is notified.
    assert rows[-1][1] == 90


def test_bad_completions_and_partial_read_failure(native_io):
    rows = check(native_io, ["open 0", "write 0 7 77", "start 0 0 8 1 64",
                            "finish 0 0 1 8 9", "finish 0 1 0 8 9", "finish 0 1 3 8 9",
                            "finish 0 1 1 7 9", "finish 0 1 1 9 9", "finish 0 1 2 3 255",
                            "take 0 1", "read 0 0", "read 0 1", "read 0 2", "read 0 7"])
    assert all(row[0] == 0 for row in rows[3:8])
    assert rows[9][3:6] == [2, 3, oracle.fnv([255, 0, 1])]
    assert [row[1] for row in rows[10:]] == [255, 0, 1, 77]


def test_generation_never_wraps_on_reopen(native_io):
    result = subprocess.run([str(native_io), "generation"], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stdout and not result.stderr


@pytest.mark.parametrize("seed", range(10))
def test_lifecycle_against_independent_transaction_ledger(native_io, seed):
    rng = random.Random(seed)
    model = oracle.IoLedger(); commands = []; expected = []
    for _ in range(20000):
        s = rng.randrange(2); token = model.generation[s]
        token = rng.choice([token, token, token, max(0, token - 1), token + 1])
        op = rng.randrange(16)
        if op == 0: command = f"open {s}"
        elif op == 1: command = f"end {s}"
        elif op <= 4:
            command = f"start {s} {rng.choice([-1, 0, 1, 0, 1])} {rng.choice([0, 1, 8, 32, 33])} {rng.choice([0, 1, 0, 1, 2])} {rng.choice([64, 64, 127, 128])}"
        elif op == 5: command = f"cancel {s} {token}"
        elif op <= 8:
            t = model.transactions[s]
            count = rng.choice([0, 3, 32, 33, t['count'] if t else 8])
            command = f"finish {s} {token} {rng.choice([0, 1, 2, 3])} {count} {rng.randrange(256)}"
        elif op <= 10: command = f"take {s} {token}"
        elif op == 11: command = f"peek {s}"
        elif op == 12: command = f"ack {s} {token}"
        elif op == 13: command = f"read {s} {rng.randrange(34)}"
        else: command = f"write {s} {rng.randrange(34)} {rng.randrange(256)}"
        commands.append(command); expected.append(model.apply(command))
    assert run(native_io, commands) == expected
