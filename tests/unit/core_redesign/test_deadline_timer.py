"""TIME-01/02 against a nonwrapping integer clock and occurrence ledger."""
from pathlib import Path
import random
import shutil
import subprocess

import pytest


class Ledger:
    def __init__(self):
        self.now = 0xFFFFFFF0
        self.next = 0
        self.period = 0
        self.active = False
        self.occurrences = 0
        self.latest = 0
        self.last = [0, 0, 0]

    def apply(self, command):
        op, *args = command.split()
        result = False
        if op == "arm":
            delay, period = map(int, args)
            result = delay < 2**31 and period < 2**31
            if result:
                self.next = self.now + delay
                self.period = period
                self.active = True
                self.occurrences = 0
        elif op == "advance":
            self.now += int(args[0]); result = True
        elif op == "poll":
            result = self.active and self.now >= self.next
            if result:
                count = (self.now - self.next) // self.period + 1 if self.period else 1
                self.latest = self.next + (count - 1) * self.period
                self.occurrences += count
                if self.period: self.next += count * self.period
                else: self.active = False
        elif op == "take":
            result = self.occurrences > 0
            if result:
                missed = self.occurrences - 1
                self.last = [self.latest & 0xFFFFFFFF, min(missed, 0xFFFFFFFF), int(missed > 0xFFFFFFFF)]
                self.occurrences = 0
        else:
            assert op == "stop"
            result = self.active; self.active = False
        return [int(result), self.now & 0xFFFFFFFF, int(self.active), int(self.occurrences > 0),
                self.next & 0xFFFFFFFF, self.period, *self.last]


@pytest.fixture(scope="module")
def native_timer(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp03-timer") / "control"
    compiler = shutil.which("g++")
    assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("timer_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return executable


def check(executable, commands):
    model = Ledger()
    expected = [model.apply(command) for command in commands]
    result = subprocess.run([str(executable)], input="\n".join(commands) + "\n",
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    rows = [list(map(int, row.split())) for row in result.stdout.splitlines()]
    assert rows == expected
    return rows


def test_before_equal_after_wrap_and_one_shot_exactly_once(native_timer):
    rows = check(native_timer, ["arm 32 0", "advance 31", "poll", "take", "advance 1", "poll",
                                "poll", "advance 100", "poll", "take", "take"])
    assert rows[2][0] == 0 and rows[5][0] == 1
    assert rows[9][6:] == [16, 0, 0] and rows[10][0] == 0


@pytest.mark.parametrize("elapsed", [9, 10, 11])
def test_first_observation_before_at_or_after_deadline(native_timer, elapsed):
    rows = check(native_timer, ["arm 10 0", f"advance {elapsed}", "poll", "take"])
    assert rows[2][0] == rows[3][0] == int(elapsed >= 10)
    if elapsed >= 10: assert rows[-1][6:] == [0xFFFFFFFA, 0, 0]


def test_late_periods_coalesce_and_preserve_original_phase(native_timer):
    rows = check(native_timer, ["arm 0 10", "poll", "advance 35", "poll", "take", "poll",
                                "advance 5", "poll", "take"])
    assert rows[4][6:] == [14, 3, 0]
    assert rows[8][6:] == [24, 0, 0]


def test_invalid_replacement_preserves_pending_and_stop_keeps_terminal(native_timer):
    rows = check(native_timer, ["arm 0 10", "poll", "arm 2147483648 0", "arm 0 2147483648",
                                "stop", "advance 100", "poll", "take", "stop", "arm 5 0", "poll"])
    assert rows[2][0] == rows[3][0] == 0 and rows[3][3] == 1
    assert rows[7][6:] == [0xFFFFFFF0, 0, 0]
    assert rows[10][0] == 0


def test_accepted_replacement_explicitly_discards_old_notification(native_timer):
    rows = check(native_timer, ["arm 0 0", "poll", "arm 10 0", "take", "advance 10", "poll", "take"])
    assert rows[3][0] == 0 and rows[-1][6:] == [0xFFFFFFFA, 0, 0]


def test_half_range_minus_one_is_supported(native_timer):
    rows = check(native_timer, ["arm 2147483647 2147483647", "poll", "advance 2147483646", "poll",
                                "advance 1", "poll", "take", "advance 2147483647", "poll", "take"])
    assert rows[1][0] == rows[3][0] == 0 and rows[5][0] == 1
    assert rows[6][7:] == rows[9][7:] == [0, 0]


def test_saturated_missed_count_does_not_wrap_or_lose_pending(native_timer):
    commands = ["arm 0 1", "poll"]
    for _ in range(6): commands.extend(["advance 1000000000", "poll"])
    commands.extend(["stop", "take", "take", "arm 0 0", "poll", "take"])
    rows = check(native_timer, commands)
    assert rows[-5][6:] == [(0xFFFFFFF0 + 6000000000) & 0xFFFFFFFF, 0xFFFFFFFF, 1]
    assert rows[-1][7:] == [0, 0]


def test_exact_maximum_count_is_distinct_from_saturation(native_timer):
    commands = ["arm 0 1", "poll"]
    for _ in range(4): commands.extend(["advance 1000000000", "poll"])
    commands.extend(["advance 294967295", "poll", "take"])
    rows = check(native_timer, commands)
    assert rows[-1][7:] == [0xFFFFFFFF, 0]


@pytest.mark.parametrize("seed", range(10))
def test_deadlines_against_nonwrapping_clock_ledger(native_timer, seed):
    rng = random.Random(seed)
    commands = []
    # Total advancement per trace stays below half range; the separate stress
    # above crosses many wraps but observes every billion ticks (< half range).
    for _ in range(20000):
        op = rng.randrange(8)
        if op == 0:
            delay = rng.choice([0, 1, 10, 2**31 - 1, 2**31, 2**32 - 1])
            period = rng.choice([0, 1, 7, 1000, 2**31 - 1, 2**31])
            commands.append(f"arm {delay} {period}")
        elif op <= 2: commands.append(f"advance {rng.choice([0, 1, 10, 999])}")
        elif op <= 4: commands.append("poll")
        elif op <= 6: commands.append("take")
        else: commands.append("stop")
    check(native_timer, commands)
