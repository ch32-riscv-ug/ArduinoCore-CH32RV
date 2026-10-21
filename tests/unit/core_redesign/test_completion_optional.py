"""Required control and paused optional display: same commands, different contracts."""
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest

from .completion_optional_oracle import Application


@pytest.fixture(scope="module")
def completion_optional(tmp_path_factory):
    source = Path(__file__).with_name("completion_optional_driver.cpp")
    compiler = shutil.which("g++")
    assert compiler
    executable = tmp_path_factory.mktemp("completion-optional") / "control"
    command = [compiler, "-std=c++17", "-O1", "-g1", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
               str(source), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stderr
    evidence = os.environ.get("EXP03_COMPLETION_OPTIONAL_EVIDENCE")
    if evidence:
        evidence = Path(evidence)
        evidence.mkdir(parents=True, exist_ok=False)
        shutil.copy2(executable, evidence / "control")
        inputs = {}
        for name in ["completion_optional_driver.cpp", "test_completion_optional.py", "completion_optional_oracle.py",
                     "completion_fanout.hpp", "completion_delivery.hpp", "completion_arbiter.hpp", "io_lifecycle.hpp",
                     "event_executor.hpp", "work_executor.hpp", "data_parts.hpp"]:
            path = source.with_name(name)
            shutil.copy2(path, evidence / name)
            inputs[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        version = subprocess.run([compiler, "--version"], capture_output=True, text=True, check=True).stdout
        (evidence / "build.json").write_text(json.dumps({
            "command": command, "compiler_version": version, "inputs": inputs,
            "compiler_sha256": hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "stdout": result.stdout, "stderr": result.stderr, "returncode": result.returncode,
        }, indent=2) + "\n")
    return executable, evidence


def run(native, policy, commands, *, drain=True):
    executable, evidence = native
    commands = list(commands)
    if drain:
        commands += ["attach 0", "attach 1", *(["poll", "draw"] * 4)]
    oracle = Application(policy)
    expected = [oracle.apply(command) for command in commands]
    stdin = "\n".join(commands) + "\n"
    result = subprocess.run([str(executable), policy], input=stdin, capture_output=True, text=True, timeout=30)
    if evidence:
        key = hashlib.sha256((policy + "\n" + stdin).encode()).hexdigest()
        (evidence / (key + ".json")).write_text(json.dumps({
            "policy": policy, "stdin": stdin, "stdout": result.stdout, "stderr": result.stderr, "returncode": result.returncode,
        }, indent=2) + "\n")
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(actual) == len(expected)
    for index, (got, want) in enumerate(zip(actual, expected)):
        assert got == want, (index, commands[index], got, want)
    if drain:
        assert actual[-1]["available"] and actual[-1]["phase"] == 0 and actual[-1]["mailboxes"] == [0, 0]
        assert actual[-1]["produced"] == actual[-1]["taken"]
    return actual


@pytest.mark.parametrize("policy", ["all", "once", "latest"])
def test_paused_display_changes_backpressure_and_which_values_survive(completion_optional, policy):
    commands = ["produce 1", "poll", "produce 2", "poll", "produce 3", "poll", "snapshot", "draw", "poll", "draw"]
    actual = run(completion_optional, policy, commands)
    before = actual[6]
    if policy == "all":
        assert before["control"] == [1] and before["pending"] and not before["available"]
    elif policy == "once":
        assert before["control"] == [1, 2, 3] and before["dropped"] == 2
        assert actual[7]["display"] == [1]
    else:
        assert before["control"] == [1, 2, 3] and before["overwritten"] == 2
        assert actual[7]["display"] == [3]


@pytest.mark.parametrize("policy", ["all", "once", "latest"])
@pytest.mark.parametrize("target", [0, 1])
@pytest.mark.parametrize("after_capture", [False, True])
def test_removed_registration_and_explicit_reattach(completion_optional, policy, target, after_capture):
    commands = ["produce 1"]
    if after_capture:
        commands += ["poll"]
    commands += [f"detach {target}", "poll", "produce 2", "poll", "draw", f"attach {target}", "poll", "draw"]
    run(completion_optional, policy, commands)


@pytest.mark.parametrize("policy", ["all", "once", "latest"])
@pytest.mark.parametrize("full_mask", range(4))
def test_full_control_and_display_queues(completion_optional, policy, full_mask):
    commands = [f"fill {i}" for i in range(2) if full_mask & (1 << i)]
    commands += ["produce 1", "poll", "produce 2", "poll", "draw", "poll", "draw"]
    run(completion_optional, policy, commands)


@pytest.mark.parametrize("policy", ["all", "once", "latest"])
@pytest.mark.parametrize("seed", range(6))
def test_against_application_owner_ledger(completion_optional, policy, seed):
    rng = random.Random(seed)
    commands = []
    for index in range(1000):
        command = rng.choice([f"produce {index + 1}", "poll", "poll", "draw", "snapshot",
                              "detach 0", "detach 1", "attach 0", "attach 1", "fill 0", "fill 1",
                              "produce 0", "produce 65536"])
        commands.append(command)
    run(completion_optional, policy, commands)
