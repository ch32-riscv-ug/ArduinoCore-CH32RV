"""Independent integrated X0 model using transaction ledgers and whole images."""

import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location("p2_app_reference_parts", Path(__file__).with_name("oracle.py"))
_parts = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_parts)
PpsOracle, render_frame, fnv = _parts.PpsOracle, _parts.render_frame, _parts.fnv


class ApplicationOracle:
    def __init__(self, touch_period=50):
        self.touch_period = touch_period  # Default preserves the historical PREP-02 profile.
        self.pd = PpsOracle()
        self.now = 0
        self.source = (5000, 1000)
        self.touch = False
        self.next_measure = self.next_touch = 0
        self.measure_pending = self.touch_pending = False
        self.missed_measure = self.missed_touch = 0
        self.measure_done = self.touch_done = 0
        self.fail_next = set()
        self.ledger = []
        self.current = None
        self.cursor = 0
        self.latest_view = (5000, 0, 0, 0, False, True, False)
        self.revision = 1
        self.frame = None
        self.frame_revision = self.displayed_revision = self.frames = self.displayed_hash = 0
        self.offset = self.next_frame = 0

    def apply(self, line):
        op, *args = line.split()
        args = list(map(int, args))
        if op in ("attach", "detach", "key", "finish"):
            self.pd.apply(line)
        elif op == "source":
            self.source = tuple(args)
        elif op == "touch":
            self.touch = bool(args[0])
        elif op == "fault":
            self.fail_next.add(args[0])
        elif op == "time":
            self.now = args[0]
            self.pd.apply(line)
            if self.current and self.current["outcome"] is None and self.current["due"] <= self.now:
                self.current["outcome"] = self.current["success"]
        elif op == "step":
            self.step()
        else:
            raise ValueError(line)
        return self.observe()

    def step(self):
        if self.current and self.current["outcome"] is not None:
            transaction = self.current
            self.current = None
            transaction["returned"] = True
            owner, ok = transaction["owner"], transaction["outcome"]
            if owner == 1:
                if not ok:
                    self.frame = None
                    self.next_frame = self.now + 100
                else:
                    self.offset += len(transaction["payload"])
                    if self.offset == 1024:
                        self.displayed_revision = self.frame_revision
                        self.displayed_hash = fnv(self.frame)
                        self.frames += 1
                        self.frame = None
            elif owner == 2:
                self.measure_done += 1
                mv, ma = transaction["sample"]
                self.pd.apply(f"sample {int(ok)} {mv} {ma}")
            elif owner == 3 and ok:
                self.touch_done += 1
        self.pd.apply("step")
        view = (self.pd.desired, self.pd.last_confirmed, self.pd.mv, self.pd.ma,
                self.pd.confirmed is not None, self.pd.stale, self.pd.online)
        if view != self.latest_view:
            self.latest_view = view
            self.revision += 1
        if self.now >= self.next_measure:
            deadlines = (self.now - self.next_measure) // 100 + 1
            self.missed_measure += deadlines - 1 + int(self.measure_pending)
            self.measure_pending = True
            self.next_measure += 100 * deadlines
        if self.touch and self.now >= self.next_touch:
            deadlines = (self.now - self.next_touch) // self.touch_period + 1
            self.missed_touch += deadlines - 1 + int(self.touch_pending)
            self.touch_pending = True
            self.next_touch += self.touch_period * deadlines
        if self.frame is None and self.revision != self.displayed_revision and self.now >= self.next_frame:
            self.frame = render_frame(*self.latest_view)
            self.frame_revision = self.revision
            self.offset = 0
            self.next_frame = self.now + 100
        if self.current is not None:
            return
        ready = {1: self.frame is not None, 2: self.measure_pending, 3: self.touch and self.touch_pending}
        for delta in range(1, 4):
            owner = (self.cursor + delta - 1) % 3 + 1
            if not ready[owner]:
                continue
            self.cursor = owner
            payload = self.frame[self.offset:self.offset + 32] if owner == 1 else bytes(8)
            if owner == 1 and self.offset == 0:
                self.next_frame = self.now + 100
            if owner == 2:
                self.measure_pending = False
            if owner == 3:
                self.touch_pending = False
            self.current = dict(owner=owner, payload=payload, sample=self.source,
                                due=self.now + 2, success=owner not in self.fail_next,
                                outcome=None, returned=False)
            self.fail_next.discard(owner)
            self.ledger.append(self.current)
            break

    def observe(self):
        return dict(time=self.now, desired=self.pd.desired, confirmed=self.pd.last_confirmed,
                    valid=int(self.pd.confirmed is not None), pd_started=len(self.pd.transactions),
                    pd_reclaimed=sum(t["returned"] for t in self.pd.transactions),
                    accepted=self.pd.accepted, rejected=self.pd.rejected,
                    owner=self.current["owner"] if self.current else 0, active=int(self.current is not None),
                    terminal=int(self.current is not None and self.current["outcome"] is not None),
                    started=len(self.ledger), completed=sum(t["outcome"] is not None for t in self.ledger),
                    reclaimed=sum(t["returned"] for t in self.ledger),
                    display_starts=sum(t["owner"] == 1 for t in self.ledger),
                    measure_starts=sum(t["owner"] == 2 for t in self.ledger),
                    touch_starts=sum(t["owner"] == 3 for t in self.ledger), corruptions=0,
                    revision=self.revision, displayed_revision=self.displayed_revision, frames=self.frames,
                    displayed_hash=self.displayed_hash, measure_done=self.measure_done, touch_done=self.touch_done,
                    mv=self.pd.mv, ma=self.pd.ma, stale=int(self.pd.stale),
                    missed_measure=self.missed_measure, missed_touch=self.missed_touch)
