"""Finite visibility orders, NOT an ISA emulator or a C++ concurrency proof.

Events denote externally visible stores and sampled loads. Contract switches
restrict those orders; they do not implement fences. One slot, one transfer,
no reuse, speculative-load repair, cache, DMA, reset, or fairness guarantee.
"""
from itertools import permutations

EVENTS = ('write_a', 'write_b', 'publish', 'notify', 'check', 'read_a', 'read_b')


def enumerate_orders(release, acquire, ordered_notify):
    for order in permutations(EVENTS):
        at = {name: order.index(name) for name in EVENTS}
        # This consumer checks only after receiving its single notification.
        if at['notify'] > at['check']:
            continue
        if release and any(at[w] > at['publish'] for w in ('write_a', 'write_b')):
            continue
        if acquire and any(at['check'] > at[r] for r in ('read_a', 'read_b')):
            continue
        if ordered_notify and at['publish'] > at['notify']:
            continue
        accepted = at['publish'] < at['check']
        payload = tuple(int(at[w] < at[r]) for w, r in
                        (('write_a', 'read_a'), ('write_b', 'read_b')))
        yield {'order': order, 'accepted': accepted, 'payload': payload,
               'bad_read': accepted and payload != (1, 1),
               'missed_once': not accepted}


def summarize(release, acquire, ordered_notify):
    rows = list(enumerate_orders(release, acquire, ordered_notify))
    bad = [r for r in rows if r['bad_read']]
    missed = [r for r in rows if r['missed_once']]
    return {'release': release, 'acquire': acquire,
            'ordered_notify': ordered_notify, 'orders': len(rows),
            'accepted': sum(r['accepted'] for r in rows),
            'bad_reads': len(bad), 'missed_once': len(missed),
            'bad_witness': bad[0] if bad else None,
            'missed_witness': missed[0] if missed else None}
