"""Serial-context data prototypes against deque and byte-stream reference models."""

from collections import deque
from pathlib import Path
import random
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def native_data(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp02-data") / "control"
    compiler = shutil.which("g++")
    assert compiler, "host g++ required"
    result = subprocess.run([compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             str(Path(__file__).with_name("data_driver.cpp")), "-o", executable],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, kind, commands):
    result = subprocess.run([executable, kind], input="\n".join(commands) + "\n", capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return [list(map(int, row.split())) for row in result.stdout.splitlines()]


def fnv(data):
    value = 2166136261
    for byte in data: value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


@pytest.mark.parametrize("kind,capacity", [("queue1", 1), ("queue4", 4), ("queue16", 16), ("view4", 4)])
@pytest.mark.parametrize("seed", range(10))
def test_fifo_capacity_wrap_and_wake(native_data, kind, capacity, seed):
    rng = random.Random(seed)
    commands, expected = [], []
    values = deque(); last = -1; wake = False
    for _ in range(10000):
        choice = rng.randrange(5)
        if choice < 3:
            value = rng.randint(-100000, 100000); commands.append(f"push {value}")
            ok = len(values) < capacity
            if ok:
                wake |= not values
                values.append(value)
        elif choice == 3:
            commands.append("pop"); ok = bool(values)
            if ok: last = values.popleft()
        else:
            commands.append("clear"); wake = False; ok = True
        expected.append([int(ok), len(values), last, int(wake)])
    assert run(native_data, kind, commands) == expected


@pytest.mark.parametrize("capacity", [16, 64, 256])
@pytest.mark.parametrize("seed", range(10))
def test_ring_partial_operations_and_wrap(native_data, capacity, seed):
    rng = random.Random(seed)
    stream = deque(); commands, expected = [], []
    for _ in range(10000):
        length = rng.choice([0, 1, capacity - 1, capacity, capacity + 1])
        if rng.randrange(2):
            start = rng.randrange(256); accepted = min(length, capacity - len(stream))
            commands.append(f"write {length} {start}")
            stream.extend((start + i) & 255 for i in range(accepted)); digest = 0
        else:
            commands.append(f"read {length}"); accepted = min(length, len(stream))
            digest = fnv([stream.popleft() for _ in range(accepted)])
        expected.append([accepted, len(stream), digest])
    assert run(native_data, f"ring{capacity}", commands) == expected


def test_ring_loans_hide_uncommitted_data_and_hold_read_storage(native_data):
    commands = ["reserve", "fill 42", "read 16", "commit 17", "commit 4", "peek", "read 4",
                "write 16 0", "consume 5", "consume 4", "read 16"]
    rows = run(native_data, "ring16", commands)
    assert rows[0] == [16, 0, 0]
    assert rows[2] == [0, 0, fnv([])]
    assert rows[3] == [0, 0, 0]  # Invalid commit leaves the outstanding loan.
    assert rows[5] == [4, 4, fnv([42] * 4)]
    assert rows[6] == [0, 4, fnv([])]
    assert rows[7] == [12, 16, 0]
    assert rows[8][0] == 0 and rows[9][1] == 12
    assert rows[-1] == [12, 0, fnv(range(12))]


@pytest.mark.parametrize("capacity,maximum", [(32, 8), (128, 32)])
@pytest.mark.parametrize("seed", range(10))
def test_messages_are_accepted_and_consumed_whole(native_data, capacity, maximum, seed):
    rng = random.Random(seed)
    messages = deque(); used = 0; last_length = 999; commands, expected = [], []
    for _ in range(10000):
        digest = 0
        if rng.randrange(2):
            length = rng.choice([0, 1, maximum, maximum + 1]); start = rng.randrange(256)
            commands.append(f"push {length} {start}")
            ok = length <= maximum and used + length + 2 <= capacity
            if ok:
                messages.append(bytes((start + i) & 255 for i in range(length))); used += length + 2
        else:
            output_capacity = rng.choice([0, 1, maximum])
            commands.append(f"pop {output_capacity}")
            ok = bool(messages) and len(messages[0]) <= output_capacity
            if ok:
                value = messages.popleft(); last_length = len(value); used -= last_length + 2
                digest = fnv(value)
        expected.append([int(ok), used, last_length, digest])
    assert run(native_data, f"message{capacity}", commands) == expected


def test_flags_semaphore_lock_pool_and_retired_generations(native_data):
    result = subprocess.run([native_data, "sync"], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout == "ok\n" and not result.stderr
