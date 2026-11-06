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


def test_the_address_is_the_only_setting_and_env_does_not_enable_execution(runner, monkeypatch, tmp_path):
    monkeypatch.setenv('CH32RV_PROBE', 'oep://30eda0e31108/x035')
    monkeypatch.setenv('OEP_HW_RESULTS', str(tmp_path / 'results'))
    a, _ = runner.parse_options([])
    b, _ = runner.parse_options([])
    assert not a.execute
    assert a.probe == ['oep://30eda0e31108/x035']
    assert a.out.parent == tmp_path / 'results' and a.out != b.out
    assert not a.out.exists()
    explicit, _ = runner.parse_options(['--probe', 'wchlink://FB868F068059', '--probe', 'oep://30eda0e31108/x035s3',
                                        '--out', 'run'])
    assert explicit.probe == ['wchlink://FB868F068059', 'oep://30eda0e31108/x035s3'] and explicit.out == Path('run')


def test_no_address_does_not_search_for_equipment(runner, monkeypatch, tmp_path):
    monkeypatch.delenv('CH32RV_PROBE', raising=False)
    monkeypatch.setattr(runner.places, 'REPO', tmp_path)      # no tests/.env there
    with pytest.raises(SystemExit) as error:
        runner.parse_options(['--out', 'run'])
    assert error.value.code == 2


def test_duplicate_addresses_are_refused(runner):
    with pytest.raises(SystemExit):
        runner.parse_options(['--probe', 'oep://30eda0e31108/x035', '--probe', 'OEP://30EDA0E31108/X035', '--out', 'r'])


def test_restarts_are_startup_lines_after_the_first_line_of_a_test(runner, tmp_path):
    logs = tmp_path / 'logs' / 'pytest-embedded' / 'run'
    (logs / 'test_a').mkdir(parents=True)
    (logs / 'test_b').mkdir(parents=True)
    (logs / 'test_c').mkdir(parents=True)
    (logs / 'test_a' / 'dut.log').write_text('STARTUP external\r\nREADY 1 x\r\n')     # the boot the console caught
    (logs / 'test_b' / 'dut.log').write_text('READY 2 x\r\nXFER 00\r\nSTARTUP software\r\nXFER 00\r\n')
    (logs / 'test_c' / 'dut.log').write_text('')
    assert runner.restarts(tmp_path / 'logs') == [('test_b', 'software')]
    assert runner.restarts(tmp_path / 'none') == []
