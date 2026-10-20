"""Preparatory RUN-01/02 tests; not an I/O or whole-candidate comparison."""
from pathlib import Path
import random
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def native_work(tmp_path_factory):
    executable = tmp_path_factory.mktemp("exp03-work") / "control"
    compiler = shutil.which("g++")
    assert compiler
    result = subprocess.run([
        compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        str(Path(__file__).with_name("work_driver.cpp")), "-o", str(executable),
    ], capture_output=True, text=True, timeout=60)
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
def test_finite_snapshot_fairness_and_no_reentry(native_work, count, quota):
    assert not run(native_work, "finite", count, quota)


def test_m_s_u_and_forbidden_contexts(native_work):
    assert not run(native_work, "profile")


def test_removal_reuse_and_generation_retirement(native_work):
    assert not run(native_work, "lifetime")


@pytest.mark.parametrize("count", [1, 4, 8])
@pytest.mark.parametrize("seed", range(10))
def test_ready_and_cursor_against_independent_model(native_work, count, seed):
    rng = random.Random(seed)
    ready = set(); cursor = 0; self_post = cross_post = False
    commands = []; expected = []
    for _ in range(10000):
        calls = []
        choice = rng.randrange(5)
        if choice < 2:
            index = rng.randrange(count); ready.add(index); commands.append(f"post {index}")
        elif choice == 2:
            self_post, cross_post = bool(rng.randrange(2)), bool(rng.randrange(2))
            commands.append(f"mode {int(self_post)} {int(cross_post)}")
        else:
            quota = rng.choice([0, 1, 4, 8]); commands.append(f"turn {quota}")
            order = [(cursor + i) % count for i in range(count)]
            selected = [i for i in order if i in ready][:quota]
            ready.difference_update(selected)
            for i in selected:
                calls.append(i)
                if self_post: ready.add(i)
                if cross_post: ready.add((i + 1) % count)
            if selected: cursor = (selected[-1] + 1) % count
        expected.append("".join(f"{i}," for i in calls) + ":" +
                        "".join(str(int(i in ready)) for i in range(count)))
    assert run(native_work, count, commands="\n".join(commands) + "\n") == expected
