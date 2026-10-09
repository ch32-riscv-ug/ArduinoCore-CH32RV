"""Explicit paths and result roots must resolve before any hardware tools run."""
import importlib.util
from pathlib import Path
import sys

import pytest

REPO = Path(__file__).resolve().parents[2]


@pytest.fixture
def runner(monkeypatch):
    monkeypatch.syspath_prepend(str(REPO / 'tests'))
    spec = importlib.util.spec_from_file_location('hardware_runner', REPO / 'tests/run_hardware.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_paths_are_explicit_and_env_does_not_enable_execution(runner, monkeypatch, tmp_path):
    monkeypatch.setenv('CH32_TEST_CONFIG', str(tmp_path / 'config.local.toml'))
    monkeypatch.setenv('OEP_HW_RESULTS', str(tmp_path / 'results'))
    monkeypatch.setenv('OEP_HW_LOCK', str(tmp_path / 'shared.lock'))
    a, _ = runner.parse_options([])
    b, _ = runner.parse_options([])
    assert not a.execute
    assert a.bench == tmp_path / 'config.local.toml'
    assert a.out.parent == tmp_path / 'results' and a.out != b.out
    assert not a.out.exists()
    explicit, _ = runner.parse_options(['--bench', 'other.toml', '--out', 'run', '--lock', 'other.lock'])
    assert explicit.bench == Path('other.toml') and explicit.out == Path('run')
    assert explicit.lock == 'other.lock'


def test_no_config_does_not_search_files(runner, monkeypatch):
    monkeypatch.delenv('CH32_TEST_CONFIG', raising=False)
    with pytest.raises(SystemExit) as error:
        runner.parse_options(['--out', 'run'])
    assert error.value.code == 2
