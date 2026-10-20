"""Targeted FIFO/coalesced delivery and sticky I/O/timer integration contracts."""
from collections import deque
from pathlib import Path
import random
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def native_event(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp03-event") / "control"
    compiler = shutil.which("g++")
    assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("event_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, *args, commands=None):
    result = subprocess.run([str(executable), *map(str, args)], input=commands,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return result.stdout.splitlines()


@pytest.mark.parametrize("count", [1, 4, 8])
@pytest.mark.parametrize("quota", [1, 4, 8])
def test_snapshot_finite_fair_nonrecursive_and_slot_not_freed_early(native_event, count, quota):
    assert not run(native_event, "finite", count, quota)


def test_m_s_u_context_matrix(native_event):
    assert not run(native_event, "profile")


def test_unregister_and_generation_retirement(native_event):
    assert not run(native_event, "lifetime")


@pytest.mark.parametrize("count", [1, 2, 4])
def test_fanout_preserves_each_destination_acceptance(native_event, count):
    assert not run(native_event, "fanout", count)


@pytest.mark.parametrize("close_during_turn", [False, True])
def test_full_mailbox_io_terminal_timer_and_end_before_delivery(native_event, close_during_turn):
    assert not run(native_event, "bridge", int(close_during_turn))


class Ledger:
    def __init__(self, count, capacity):
        self.capacity = capacity
        self.queues = [deque() for _ in range(count)]
        self.bits = [0] * count
        self.cursor = 0
        self.self_post = self.cross_post = False

    def send(self, index, value):
        queue = self.queues[index]
        if len(queue) == self.capacity: return 1
        queue.append(value & 0xFFFFFFFF)
        return 0

    def apply(self, command):
        op, *args = command.split(); args = list(map(int, args))
        result = 0; calls = []; sends = []
        if op == "send": result = self.send(*args)
        elif op == "notify":
            index, bits = args; result = int(bits != 0); self.bits[index] |= bits
        elif op == "mode": self.self_post, self.cross_post = map(bool, args)
        else:
            assert op == "turn"
            quota, = args; count = len(self.queues)
            order = [(self.cursor + i) % count for i in range(count)]
            selected = [i for i in order if self.queues[i] or self.bits[i]][:quota]
            frozen = [(i, self.queues[i][0] if self.queues[i] else None, self.bits[i]) for i in selected]
            for i in selected: self.bits[i] = 0
            if selected: self.cursor = (selected[-1] + 1) % count
            for i, value, bits in frozen:
                if value is not None: assert self.queues[i].popleft() == value
                calls.append(f"{i},{int(value is not None)},{value if value is not None else 0},{bits};")
                value = value if value is not None else 42
                if self.self_post:
                    sends.append(self.send(i, value + 1)); self.bits[i] |= 1
                if self.cross_post:
                    other = (i + 1) % count
                    sends.append(self.send(other, value + 10)); self.bits[other] |= 2
            result = len(selected) * 101
        state = "".join(f"{len(q)},{bits};" for q, bits in zip(self.queues, self.bits))
        return f"{result}|{''.join(calls)}|{''.join(f'{r},' for r in sends)}|{state}"


@pytest.mark.parametrize("count", [1, 4, 8])
@pytest.mark.parametrize("capacity", [1, 4, 8])
@pytest.mark.parametrize("seed", range(5))
def test_fifo_snapshot_and_coalescing_against_ledger(native_event, count, capacity, seed):
    rng = random.Random(seed); model = Ledger(count, capacity)
    commands = []; expected = []
    for _ in range(10000):
        op = rng.randrange(7)
        if op <= 1: command = f"send {rng.randrange(count)} {rng.randrange(2**32)}"
        elif op == 2: command = f"notify {rng.randrange(count)} {rng.choice([0, 1, 2, 0x80000000, 0xFFFFFFFF])}"
        elif op == 3: command = f"mode {rng.randrange(2)} {rng.randrange(2)}"
        else: command = f"turn {rng.choice([0, 1, 4, 8])}"
        commands.append(command); expected.append(model.apply(command))
    # Drain accepted payloads, including those still queued at the trace end.
    for command in ["mode 0 0", *(["turn 8"] * (capacity + 1))]:
        commands.append(command); expected.append(model.apply(command))
    assert run(native_event, "protocol", count, capacity, commands="\n".join(commands) + "\n") == expected
