"""Every bundled example builds for every series it says it supports.

The fast check (test_examples.py) builds on two boards, which is what belongs
in front of someone waiting. This is the wide one: one board per series, all
24 of them, about 20 minutes. It is opt-in (`pytest --sweep`) and meant for
GitHub Actions, where the only cost is wall-clock nobody is watching and the
interface is a failure notification. There it runs as parallel shards
(`--sweep-shard K/N`, tests-legacy/conftest.py), each a few minutes.

Which series an example is built for comes from the example's own `requires:`
line - see tests-legacy/compile/compile_examples.py and
docs/examples-build-rules.ja.md.
"""
import pytest

from loader import load

pytestmark = [pytest.mark.slow, pytest.mark.sweep]

harness = load("tests-legacy/build/compile/compile_examples.py", "compile_examples")


def test_every_example_builds_for_every_series_it_claims(
        repo, gcc_bin, arduino_cli, workdir, sweep_boards):
    boards, sharded = sweep_boards
    result = harness.run(workdir / "examples-sweep", boards)
    # A sweep that built almost nothing would pass silently otherwise: every
    # example skipped everywhere still leaves an empty failure list. A shard
    # holds a few series, so it only has to have built something.
    if sharded:
        assert result["built"], (boards, result["skipped"])
    else:
        assert len(result["built"]) > len(result["examples"]), result["skipped"]
