"""RUN-03: explicit producer capabilities and A/B/C actual host dispatch."""
from itertools import product
from pathlib import Path
import random
import shutil
import subprocess

import pytest

FIELDS = "candidate producer where profile opted irq remote clock context timeout due jump now preloaded stop nested".split()


def scenario(**kwargs):
    values = dict(candidate=0, producer=0, where=0, profile=0, opted=0, irq=1, remote=1, clock=1,
                  context=0, timeout=9, due=3, jump=1, now=0xFFFFFFFC, preloaded=0, stop=0, nested=0)
    values.update(kwargs)
    return values


def supports(s):
    if s["producer"] == 0: return bool(s["irq"])
    if s["producer"] == 3: return bool(s["remote"])
    return s["where"] == 0 or (s["profile"] != 0 and
           (s["producer"] == 1 or (s["profile"] == 2 and s["opted"])))


def model(mode, s):
    elapsed = drives = callbacks = produced = 0
    available = bool(s["preloaded"])
    value = 99; nested = 8
    def row(status):
        return [status, elapsed, drives, callbacks, value, int(available), produced,
                (s["now"] + elapsed) & 0xFFFFFFFF, nested]
    if mode == "try":
        if available: available = False; value = 17
        return row(0 if value == 17 else 1)
    if mode == "blocking" and s["context"] != 0: return row(3)
    if s["timeout"] >= 2**31: return row(4)
    if mode == "async" and s["context"] in [1, 3]: return row(3)
    if available: available = False; value = 17; return row(0)
    if s["timeout"] == 0: return row(2)
    if mode == "blocking":
        if not s["clock"]: return row(6)
        if not supports(s): return row(5)
    for _ in range(16):
        drives += 1
        if s["stop"]: return row(7 if mode == "blocking" else 1)
        if s["clock"]: elapsed += s["jump"]
        if elapsed >= s["due"] and supports(s):
            produced = 1; value = 17
            if s["producer"] in [1, 2]:
                callbacks = 1
                if s["nested"]: nested = 3
            return row(0)
        if elapsed >= s["timeout"]: return row(2)
    assert mode == "async", "blocking fixture must have a progressing clock and reachable producer"
    return row(1)


@pytest.fixture(scope="module")
def native_wait(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp03-wait") / "control"
    compiler = shutil.which("g++"); assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("wait_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return executable


def run(executable, mode, scenarios=()):
    commands = [" ".join(str(s[name]) for name in FIELDS) for s in scenarios]
    result = subprocess.run([str(executable), mode], input="\n".join(commands) + "\n",
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    return [list(map(int, row.split())) for row in result.stdout.splitlines()]


@pytest.mark.parametrize("candidate", [0, 1, 2], ids=["A", "B", "C"])
@pytest.mark.parametrize("mode", ["blocking", "async"])
def test_producer_opportunity_profile_matrix(native_wait, candidate, mode):
    cases = [scenario(candidate=candidate, producer=p, where=w, profile=f, opted=o, nested=1)
             for p, w, f, o in product(range(4), range(3), range(3), range(2))]
    assert run(native_wait, mode, cases) == [model(mode, s) for s in cases]


def test_literal_boundary_outcomes_and_rejected_wait_never_consumes(native_wait):
    cases = [scenario(producer=2, where=2, profile=1),
             scenario(producer=2, where=2, profile=2, opted=1),
             scenario(producer=0, irq=0), scenario(producer=3, remote=0),
             scenario(context=2, preloaded=1), scenario(clock=0), scenario(clock=0, preloaded=1),
             scenario(timeout=0), scenario(timeout=2**31, preloaded=1),
             scenario(due=9), scenario(due=10), scenario(due=9, jump=5), scenario(stop=1)]
    rows = run(native_wait, "blocking", cases)
    assert [r[0] for r in rows] == [5, 0, 5, 5, 3, 6, 0, 2, 4, 0, 2, 0, 7]
    assert rows[4][4:6] == rows[8][4:6] == [99, 1]
    assert rows[9][1] == rows[10][1] == 9
    assert rows[11][1] == 10  # Late observation is visible, not a deadline guarantee.
    assert rows == [model("blocking", s) for s in cases]


def test_try_never_progresses_a_producer(native_wait):
    cases = [scenario(candidate=c, producer=p, preloaded=ready, context=2)
             for c, p, ready in product(range(3), range(4), range(2))]
    rows = run(native_wait, "try", cases)
    assert rows == [model("try", s) for s in cases]
    assert all(row[2:4] == [0, 0] for row in rows)


def test_async_poll_is_nonblocking_and_cancel_preserves_data(native_wait):
    assert run(native_wait, "contracts") == []


@pytest.mark.parametrize("mode", ["blocking", "async", "try"])
def test_generated_clock_wrap_context_and_capability_cases(native_wait, mode):
    rng = random.Random(76231)
    cases = []
    for _ in range(1500):
        cases.append(scenario(candidate=rng.randrange(3), producer=rng.randrange(4), where=rng.randrange(3),
                     profile=rng.randrange(3), opted=rng.randrange(2), irq=rng.randrange(2), remote=rng.randrange(2),
                     clock=rng.randrange(2), context=rng.choice([0, 2] if mode == "try" else range(4)),
                     timeout=rng.choice([0, 1, 5, 9, 12, 2**31 - 1, 2**31, 2**32 - 1]),
                     due=rng.randrange(17), jump=rng.choice([1, 2, 5]), now=rng.choice([0, 2**32 - 4]),
                     preloaded=rng.randrange(2), stop=rng.randrange(2), nested=1))
    assert run(native_wait, mode, cases) == [model(mode, s) for s in cases]
