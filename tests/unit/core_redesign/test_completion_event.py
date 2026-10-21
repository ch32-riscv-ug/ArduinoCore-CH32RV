"""Actual arbiter/EventExecutor composition; no production API or hardware."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def completion_event(tmp_path_factory):
    source = Path(__file__).with_name("completion_event_driver.cpp")
    compiler = shutil.which("g++")
    assert compiler
    executable = tmp_path_factory.mktemp("completion-event") / "control"
    flags = ["-std=c++17", "-O1", "-g1", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             "-fno-omit-frame-pointer"]
    command = [compiler, *flags, str(source), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stderr
    evidence = os.environ.get("EXP03_COMPLETION_EVENT_EVIDENCE")
    if evidence:
        evidence = Path(evidence)
        evidence.mkdir(parents=True, exist_ok=False)
        shutil.copy2(executable, evidence / "control")
        inputs = {}
        for name in ["completion_event_driver.cpp", "test_completion_event.py", "completion_arbiter.hpp",
                     "io_lifecycle.hpp", "event_executor.hpp", "work_executor.hpp", "data_parts.hpp"]:
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


def run(native, *args):
    executable, evidence = native
    result = subprocess.run([str(executable), *map(str, args)], capture_output=True, text=True, timeout=20)
    if evidence:
        (evidence / ("-".join(map(str, args)) + ".json")).write_text(json.dumps({
            "args": args, "stdout": result.stdout, "stderr": result.stderr, "returncode": result.returncode,
        }, indent=2) + "\n")
    assert result.returncode == 0, result.stderr
    assert not result.stderr
    value = json.loads(result.stdout)
    assert value["all_buffers_returned"] is True
    return value


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("domain", range(3))
@pytest.mark.parametrize("profile", range(3))
@pytest.mark.parametrize("opportunity", range(3))
def test_delivery_permissions(completion_event, policy, domain, profile, opportunity):
    assert run(completion_event, policy, "matrix", domain, profile, opportunity) == {
        "calls": 1, "collected": 1, "skipped": 0, "all_buffers_returned": True,
    }


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("scenario,expected", [
    ("full", (2, 2, 0)), ("end", (1, 0, 1)),
    ("stale", (2, 1, 1)), ("reentrant", (1, 1, 0)),
])
def test_lifetime_and_backpressure(completion_event, policy, scenario, expected):
    value = run(completion_event, policy, scenario)
    assert (value["calls"], value["collected"], value["skipped"]) == expected


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("early_ack", [0, 1])
def test_registration_loss_and_ack_timing(completion_event, policy, early_ack):
    value = run(completion_event, policy, "unbind", early_ack)
    assert value["collected"] == 1 - early_ack
    assert value["calls"] == 1 - early_ack


@pytest.mark.parametrize("policy", ["dedicated", "pooled"])
@pytest.mark.parametrize("context", range(4))
def test_forbidden_context_retains_source(completion_event, policy, context):
    assert run(completion_event, policy, "context", context)["collected"] == 1
