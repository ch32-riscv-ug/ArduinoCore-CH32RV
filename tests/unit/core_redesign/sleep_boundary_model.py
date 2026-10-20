"""Finite interleavings of abstract idle/wait protocols, not a WCH emulator.

The latch and masked-wake properties are explicit assumptions. An MCU adapter
must establish them independently. Work is a sticky condition, not an edge count.
"""
from fractions import Fraction
from itertools import combinations_with_replacement

PROTOCOLS = {
    'check-wait': ('check', 'wait'),
    'check-drain-wait': ('check', 'drain', 'wait'),
    'drain-check-wait': ('drain', 'check', 'wait'),
    'guard-check-wait': ('mask', 'check', 'atomic-wait-unmask'),
    'guard-check-unmask-wait': ('mask', 'check', 'unmask', 'wait'),
}


class Machine:
    def __init__(self, latched, masked_wake, stale=False):
        self.latched = latched
        self.masked_wake = masked_wake
        self.event = stale and latched
        self.masked = False
        self.hardware_pending = False
        self.work = False
        self.sleeping = False
        self.aborted = False
        self.restore_on_wake = False
        self.trace = []

    def state(self):
        return dict(event=self.event, masked=self.masked, irq=self.hardware_pending,
                    work=self.work, asleep=self.sleeping, aborted=self.aborted)

    def unmask(self):
        self.masked = False
        if self.hardware_pending:
            self.hardware_pending = False
            self.work = True

    def wake(self):
        self.sleeping = False
        if self.restore_on_wake:
            self.restore_on_wake = False
            self.unmask()

    def post(self):
        if self.latched:
            self.event = True
        if self.masked:
            self.hardware_pending = True
        else:
            # The modeled ISR runs to completion before normal code resumes.
            self.work = True
        if self.sleeping and (self.event or not self.masked or self.masked_wake):
            # A waking wait consumes its event, even if work remains sticky.
            self.event = False
            self.wake()
        self.trace.append(('post', self.state()))

    def step(self, action):
        if self.aborted:
            return
        if action == 'mask':
            self.masked = True
        elif action == 'unmask':
            self.unmask()
        elif action == 'drain':
            self.event = False
        elif action == 'check':
            if self.work:
                self.aborted = True
                self.unmask()
        elif action in ('wait', 'atomic-wait-unmask'):
            if action == 'atomic-wait-unmask':
                self.restore_on_wake = True
            immediate = self.event or (self.masked and self.hardware_pending and self.masked_wake)
            self.event = False
            if immediate:
                self.wake()
            else:
                self.sleeping = True
        else:
            raise ValueError(action)
        self.trace.append((action, self.state()))


def run(protocol, latched, masked_wake, posts, stale=False):
    """post slots include before the first action and after the final wait."""
    actions = PROTOCOLS[protocol]
    if any(p < 0 or p > len(actions) for p in posts):
        raise ValueError('post slot')
    machine = Machine(latched, masked_wake, stale)
    for slot in range(len(actions)+1):
        for _ in range(posts.count(slot)):
            machine.post()
        if slot < len(actions):
            machine.step(actions[slot])
    return dict(stranded=machine.sleeping and (machine.work or machine.hardware_pending),
                final=machine.state(), trace=machine.trace)


def enumerate_protocol(protocol, backend, max_posts=2):
    scenarios = []
    for stale in ((False, True) if backend['latched'] else (False,)):
        for count in range(max_posts+1):
            for posts in combinations_with_replacement(range(len(PROTOCOLS[protocol])+1), count):
                result = run(protocol, backend['latched'], backend['masked_wake'], list(posts), stale)
                scenarios.append(dict(posts=list(posts), stale=stale, **result))
    return scenarios


def awu_intervals(prescalers, maximum_window, frequencies):
    """Ideal whole-window calculation; phase, startup, ISR and external wake omitted."""
    low, nominal, high = frequencies
    if not 0 < low <= nominal <= high or not 0 <= maximum_window <= 63:
        raise ValueError('clock/window')
    for code, divisor in prescalers:
        if not 0 <= code <= 15 or divisor <= 0:
            raise ValueError('prescaler')
        for window in range(maximum_window+1):
            ticks = (window+1)*divisor
            yield dict(code=code, divisor=divisor, window=window, ticks=ticks,
                       minimum=Fraction(ticks, high), nominal=Fraction(ticks, nominal),
                       maximum=Fraction(ticks, low))


def choose_counter_interval(candidates, target, policy):
    """Offline comparison of counter duration policies; no wake deadline promise."""
    candidates = list(candidates)
    if policy == 'nearest-nominal':
        return min(candidates, key=lambda row: (abs(row['nominal']-target), row['ticks'], row['code']))
    if policy == 'counter-at-least':
        eligible = [row for row in candidates if row['minimum'] >= target]
        return min(eligible, key=lambda row: (row['ticks'], row['code']), default=None)
    if policy == 'counter-at-most':
        eligible = [row for row in candidates if row['maximum'] <= target]
        return max(eligible, key=lambda row: (row['ticks'], -row['code']), default=None)
    raise ValueError(policy)
