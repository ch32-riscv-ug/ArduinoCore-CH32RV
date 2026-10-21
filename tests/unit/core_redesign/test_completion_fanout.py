"""Fan-out of copied completion metadata versus competing source-token hints."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def completion_fanout(tmp_path_factory):
    source = Path(__file__).with_name("completion_fanout_driver.cpp")
    compiler = shutil.which("g++")
    assert compiler
    executable = tmp_path_factory.mktemp("completion-fanout") / "control"
    command = [compiler, "-std=c++17", "-O1", "-g1", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
               str(source), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, result.stderr
    evidence = os.environ.get("EXP03_COMPLETION_FANOUT_EVIDENCE")
    if evidence:
        evidence = Path(evidence)
        evidence.mkdir(parents=True, exist_ok=False)
        shutil.copy2(executable, evidence / "control")
        inputs = {}
        for name in ["completion_fanout_driver.cpp", "test_completion_fanout.py", "completion_fanout.hpp",
                     "completion_delivery.hpp", "completion_arbiter.hpp", "io_lifecycle.hpp",
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


def run(native, policy, targets, capacity, scenario, mask=0):
    executable, evidence = native
    args = [policy, targets, capacity, scenario, mask]
    result = subprocess.run([str(executable), *map(str, args)], capture_output=True, text=True, timeout=20)
    if evidence:
        (evidence / ("-".join(map(str, args)) + ".json")).write_text(json.dumps({
            "args": args, "stdout": result.stdout, "stderr": result.stderr, "returncode": result.returncode,
        }, indent=2) + "\n")
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    value = json.loads(result.stdout)
    assert value["all_buffers_returned"] and value["publication_released"]
    return value


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("capacity", [1, 2])
@pytest.mark.parametrize("targets,mask", [(n, mask) for n in [1, 2, 4] for mask in range(1 << n)])
@pytest.mark.parametrize("scenario", ["full", "remove"])
def test_each_full_or_removed_recipient_subset(completion_fanout, policy, capacity, targets, mask, scenario):
    value = run(completion_fanout, policy, targets, capacity, scenario, mask)
    assert value["receipts"] == targets
    assert value["rejected"] == (mask.bit_count() * capacity if scenario == "full" else 0)


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("capacity", [1, 2])
@pytest.mark.parametrize("targets,mask", [(n, mask) for n in [1, 2, 4] for mask in range(1, 1 << n)])
def test_required_recipient_subset(completion_fanout, policy, capacity, targets, mask):
    value = run(completion_fanout, policy, targets, capacity, "subset", mask)
    assert (value["receipts"], value["rejected"]) == (mask.bit_count(), 0)


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("capacity", [1, 2])
@pytest.mark.parametrize("targets", [1, 2, 4])
@pytest.mark.parametrize("scenario", ["hint", "retained", "stale", "invalid", "retire", "end-after-copy", "end-before-copy", "stalled"])
def test_ownership_and_publication_lifetime(completion_fanout, policy, capacity, targets, scenario):
    value = run(completion_fanout, policy, targets, capacity, scenario)
    expected = {
        "hint": (1, targets - 1), "retained": (targets * 2, 0),
        "stale": (targets * 2, targets), "invalid": (targets, 1),
        "retire": (0, 0), "end-after-copy": (targets, 0), "end-before-copy": (0, 0), "stalled": (targets * 2, 0),
    }
    assert (value["receipts"], value["rejected"]) == expected[scenario]


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("capacity", [1, 2])
@pytest.mark.parametrize("targets", [2, 4])
def test_remove_later_snapshot_target_inside_handler(completion_fanout, policy, capacity, targets):
    value = run(completion_fanout, policy, targets, capacity, "during-turn")
    assert (value["receipts"], value["rejected"]) == (targets, 0)
