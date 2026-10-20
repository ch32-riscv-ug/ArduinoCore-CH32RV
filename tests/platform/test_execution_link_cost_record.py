"""Board-free checks for evidence loss and false successful EXP-00 records."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest


SCRIPT = Path(__file__).with_name("record_execution_link_cost.py")
spec = importlib.util.spec_from_file_location("execution_link_record", SCRIPT)
record = importlib.util.module_from_spec(spec)
spec.loader.exec_module(record)


def test_child_failure_retains_output_and_exit_code(tmp_path):
    (tmp_path / "logs").mkdir()
    recorder = record.Recorder(tmp_path)
    with pytest.raises(ValueError, match="failed \\(7\\)"):
        recorder.run([sys.executable, "-c",
                      "import sys; print('partial'); print('problem',file=sys.stderr); sys.exit(7)"])
    commands = json.loads((tmp_path / "commands.json").read_text())
    assert commands[0]["returncode"] == 7
    assert (tmp_path / commands[0]["stdout"]).read_text() == "partial\n"
    assert (tmp_path / commands[0]["stderr"]).read_text() == "problem\n"


def test_missing_compiler_produces_failure_manifest_not_success(tmp_path):
    out = tmp_path / "failed"
    result = subprocess.run([sys.executable, SCRIPT, "--compiler", tmp_path / "missing-g++",
                             "--out", out], capture_output=True, text=True)
    assert result.returncode != 0
    summary = json.loads((out / "summary.json").read_text())
    assert summary["status"] == "failed"
    assert "compiler does not exist" in summary["error"]
    assert summary["measurements"]["cycles"] is None
    assert (out / "manifest.json").is_file()
    assert (out / "sha256.json").is_file()


def test_existing_output_is_never_overwritten(tmp_path):
    marker = tmp_path / "summary.json"
    marker.write_text("prior evidence\n")
    result = subprocess.run([sys.executable, SCRIPT, "--compiler", tmp_path / "missing-g++",
                             "--out", tmp_path], capture_output=True, text=True)
    assert result.returncode != 0
    assert marker.read_text() == "prior evidence\n"


@pytest.mark.parametrize("field,value", [
    ("flash_bytes", 25), ("static_ram_bytes", 0),
    ("delta_flash", -1), ("flash_section_bytes", 23),
])
def test_forged_metrics_are_rejected(field, value):
    row = dict(arch="rv32e", march="rv32ec_zicsr", abi="ilp32e", case="baseline",
               sections={".text": 24, ".bss": 4}, flash_bytes=24,
               flash_section_bytes=24, static_ram_bytes=4, delta_flash=0, delta_ram=0)
    row[field] = value
    with pytest.raises(ValueError):
        record.validate_row(row, {".text": 24, ".bss": 4}, 24,
                            {"user_runs": [0x20000000, 4, "B"], "user_step": [0, 16, "T"]}, (24, 4))


@pytest.mark.parametrize("change", ["rows", "symbols", "elf_sha256", "bin_sha256"])
def test_repeat_comparison_rejects_changed_evidence(change):
    first = {"rows": [{"arch": "rv32e", "case": "baseline", "flash_bytes": 24}],
             "observations": {"rv32e/baseline": {"symbols": {}, "elf_sha256": "a", "bin_sha256": "b"}}}
    second = copy.deepcopy(first)
    if change == "rows":
        second["rows"][0]["flash_bytes"] = 26
    else:
        second["observations"]["rv32e/baseline"][change] = "different"
    with pytest.raises(ValueError, match="differ"):
        record.compare(first, second)
