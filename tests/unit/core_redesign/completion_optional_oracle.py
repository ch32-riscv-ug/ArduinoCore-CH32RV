"""Application ledger: one source record, required receipts, optional snapshots."""


class Application:
    def __init__(self, policy):
        assert policy in ("all", "once", "latest")
        self.policy = policy
        self.source = None
        self.publication = None
        self.generation = 0
        self.attached = [True, True]
        self.queues = [[], []]
        self.produced = self.taken = self.offered = self.dropped = self.removed = self.overwritten = 0
        self.control, self.display = [], []
        self.latest = 0
        self.dirty = False

    def release(self):
        p = self.publication
        if p is not None and p["delivered"] == p["required"]:
            self.publication = None

    def turn(self, display):
        selected = []
        for i in range(2):
            if self.attached[i] and self.queues[i] and (i == 0 or display):
                selected.append((i, self.queues[i].pop(0)))
        for i, (kind, sequence, generation) in selected:
            if kind == "optional":
                assert self.policy == "once" and i == 1
                self.display.append(sequence)
            elif kind == "required":
                p = self.publication
                assert p is not None and p["generation"] == generation and i in p["queued"]
                p["queued"].remove(i)
                p["delivered"].add(i)
                (self.control if i == 0 else self.display).append(sequence)

    def apply(self, line):
        op, *args = line.split()
        result = 0
        if op == "produce":
            value, = map(int, args)
            if not 0 < value <= 65535:
                result = 2
            elif self.source is not None:
                result = 1
            else:
                self.source = value
                self.produced += 1
        elif op == "poll":
            if self.source is not None and self.publication is None:
                value, self.source = self.source, None
                self.generation += 1
                self.taken += 1
                self.publication = {
                    "generation": self.generation, "value": value,
                    "required": {0, 1} if self.policy == "all" else {0},
                    "queued": set(), "delivered": set(),
                }
                if self.policy == "once":
                    self.offered += 1
                    if self.attached[1] and not self.queues[1]:
                        self.queues[1].append(("optional", value, 0))
                    else:
                        self.dropped += 1
                elif self.policy == "latest":
                    self.overwritten += self.dirty
                    self.latest, self.dirty = value, True
            p = self.publication
            if p is not None:
                for i in p["required"] - p["queued"] - p["delivered"]:
                    if self.attached[i] and not self.queues[i]:
                        self.queues[i].append(("required", p["value"], p["generation"]))
                        p["queued"].add(i)
            self.turn(False)
            self.release()
        elif op == "draw":
            self.turn(True)
            if self.policy == "latest" and self.attached[1] and self.dirty:
                self.display.append(self.latest)
                self.dirty = False
            self.release()
        elif op in ("detach", "attach"):
            i, = map(int, args)
            if i > 1 or self.attached[i] == (op == "attach"):
                result = 0
            else:
                result = 1
                self.attached[i] = op == "attach"
                if op == "detach":
                    if i == 1 and self.policy == "once":
                        self.removed += len(self.queues[i])
                    self.queues[i].clear()
                elif self.publication is not None:
                    p = self.publication
                    if i in p["required"] and i not in p["delivered"]:
                        p["queued"].discard(i)
        elif op == "fill":
            i, = map(int, args)
            if i > 1 or not self.attached[i]:
                result = 2
            elif self.queues[i]:
                result = 1
            else:
                self.queues[i].append(("filler", 0, 0))
        else:
            assert op == "snapshot"
        p = self.publication
        mask = lambda key: sum(1 << i for i in p[key]) if p is not None else 0
        return {
            "result": result, "produced": self.produced, "taken": self.taken,
            "pending": self.source is not None, "available": self.source is None,
            "phase": 2 if p is not None else 0, "required": mask("required"),
            "queued": mask("queued"), "delivered": mask("delivered"),
            "mailboxes": [len(q) for q in self.queues], "attached": list(self.attached),
            "offered": self.offered, "dropped": self.dropped, "removed": self.removed,
            "overwritten": self.overwritten, "dirty": self.dirty, "latest": self.latest,
            "control": list(self.control), "display": list(self.display),
        }
