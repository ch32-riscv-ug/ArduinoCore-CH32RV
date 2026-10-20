"""Necessary adapter contracts: tests do not prove WFI/WFE behavior on a chip."""
from fractions import Fraction
import importlib.util
import json
from pathlib import Path

import pytest

UNIT = Path(__file__).parent
REPO = UNIT.parents[2]
CONFIG = json.loads((REPO/'planning/core-redesign/sleep-inputs.json').read_text())
spec = importlib.util.spec_from_file_location('sleep_boundary', UNIT/'sleep_boundary_model.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)


@pytest.mark.parametrize('backend', CONFIG['protocol_backends'], ids=lambda b: b['id'])
@pytest.mark.parametrize('protocol', list(model.PROTOCOLS))
def test_all_boundary_interleavings(protocol, backend):
    scenarios = model.enumerate_protocol(protocol, backend, CONFIG['max_posts'])
    failures = [s for s in scenarios if s['stranded']]
    # The expectation follows each *declared* backend's latch/wake capability.
    safe = ((backend['latched'] and protocol in ('check-wait', 'drain-check-wait', 'guard-check-wait', 'guard-check-unmask-wait'))
            or (backend['masked_wake'] and protocol == 'guard-check-wait'))
    assert bool(failures) != safe, (protocol, backend, failures[:1])
    for scenario in scenarios:
        if not scenario['posts']:
            assert not scenario['stranded']
        if scenario['final']['aborted']:
            assert not scenario['final']['asleep'] and not scenario['final']['masked']


def test_serviced_irq_between_check_and_wait_is_a_counterexample():
    result = model.run('check-wait', False, True, [1])
    assert result['stranded'] and result['final']['work']
    assert not result['final']['irq']  # the handler already ran


def test_draining_after_check_can_consume_the_new_wake_event():
    result = model.run('check-drain-wait', True, False, [1])
    assert result['stranded'] and result['final']['work'] and not result['final']['event']
    assert not model.run('drain-check-wait', True, False, [2])['stranded']


def test_unmasking_before_wait_reintroduces_the_gap():
    assert model.run('guard-check-unmask-wait', False, True, [3])['stranded']
    assert not model.run('guard-check-wait', False, True, [2])['stranded']
    assert model.run('guard-check-wait', False, False, [2])['stranded']


@pytest.mark.parametrize('window', CONFIG['maximum_windows_for_sensitivity'])
def test_all_awu_intervals_against_exact_products(window):
    rows = list(model.awu_intervals(CONFIG['prescalers'], window, CONFIG['lsi_hz_min_nominal_max']))
    assert len(rows) == 15*(window+1)
    for row in rows:
        expected = (row['window']+1)*row['divisor']
        assert row['ticks'] == expected
        assert row['minimum']*172000 == row['nominal']*128000 == row['maximum']*90000 == expected
        assert row['minimum'] <= row['nominal'] <= row['maximum']
    # Full hardware-width comparison and the SDK's stricter comment differ.
    assert max(r['ticks'] for r in rows) == (window+1)*61440


@pytest.mark.parametrize('window', CONFIG['maximum_windows_for_sensitivity'])
@pytest.mark.parametrize('target', CONFIG['target_seconds'])
@pytest.mark.parametrize('policy', CONFIG['policies'])
def test_interval_policy_against_tick_thresholds(window, target, policy):
    rows = list(model.awu_intervals(CONFIG['prescalers'], window, CONFIG['lsi_hz_min_nominal_max']))
    chosen = model.choose_counter_interval(rows, Fraction(target), policy)
    products = sorted(set((w+1)*d for _, d in CONFIG['prescalers'] for w in range(window+1)))
    if policy == 'counter-at-least':
        eligible = [p for p in products if p >= target*172000]
        expected = min(eligible, default=None)
    elif policy == 'counter-at-most':
        eligible = [p for p in products if p <= target*90000]
        expected = max(eligible, default=None)
    else:
        expected = min(products, key=lambda p: (abs(p-target*128000), p))
    assert (None if chosen is None else chosen['ticks']) == expected


def test_nominal_sdk_example_and_unknown_actual_elapsed():
    rows = list(model.awu_intervals(CONFIG['prescalers'], 63, CONFIG['lsi_hz_min_nominal_max']))
    example = next(r for r in rows if r['code'] == 14 and r['window'] == 25)
    assert example['nominal'] == Fraction(208, 100)
    assert example['minimum'] < 2 < example['maximum']
    # Configured duration alone is not evidence of elapsed time, especially
    # when another source can wake earlier. No logical-clock correction here.
    external_wake_elapsed = Fraction(1, 2)
    assert external_wake_elapsed < example['minimum']


@pytest.mark.parametrize('frequencies,window', [([0, 128000, 172000], 63), ([172000, 128000, 90000], 63), ([90000, 128000, 172000], 64)])
def test_invalid_interval_inputs(frequencies, window):
    with pytest.raises(ValueError): list(model.awu_intervals(CONFIG['prescalers'], window, frequencies))
