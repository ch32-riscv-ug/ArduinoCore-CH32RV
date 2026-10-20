"""Necessary publication assumptions; no H417 hardware is exercised."""
import importlib.util
from itertools import product
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    'h417_publication', Path(__file__).with_name('h417_publication_model.py'))
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)


@pytest.mark.parametrize('release,acquire,notify', list(product((False, True), repeat=3)))
def test_all_visibility_orders(release, acquire, notify):
    rows = list(model.enumerate_orders(release, acquire, notify))
    assert rows
    assert len({r['order'] for r in rows}) == len(rows)
    assert any(r['accepted'] and r['payload'] == (1, 1) for r in rows)
    assert any(r['bad_read'] for r in rows) == (not (release and acquire))
    assert any(r['missed_once'] for r in rows) == (not notify)
    for row in rows:
        if row['accepted'] and release and acquire:
            assert row['payload'] == (1, 1)
        if notify:
            assert row['accepted']


def test_notification_alone_can_expose_partially_written_payload():
    rows = model.enumerate_orders(False, True, True)
    assert any(r['accepted'] and r['payload'] == (1, 0) for r in rows)


def test_publication_without_ordered_notification_can_miss_the_only_wake():
    row = model.summarize(True, True, False)['missed_witness']
    assert row and row['order'].index('check') < row['order'].index('publish')
    # A later explicit poll sees the retained state. No upper latency bound.
    assert row['order'].index('write_a') < row['order'].index('publish')
    assert row['order'].index('write_b') < row['order'].index('publish')
