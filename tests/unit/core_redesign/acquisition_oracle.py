"""Unbounded Python lists model ownership; completion order uses append/pop.

The fake source writes a known prefix. No DMA/circular-buffer safety is implied.
"""
import copy


class Model:
    def __init__(self, samples=64, short=False):
        self.n = samples
        self.maximum = 255 if short else 2**32-1
        self.opened = False
        self.generation = 0
        self.next = 0
        self.ready = []
        self.blocks = [self.empty(), self.empty()]
        self.token = [0, 99]
        self.completion = [0, 99, 999, 999, 1]
        self.sample = 54321

    def empty(self):
        return dict(g=0, capture=0, values=[], count=0, phase=0, result=0, cancel=0, notice=0)

    def apply(self, command):
        op, *args = command.split()
        a, i, c, d, e = [*map(int, args), 0, 0, 0, 0, 0][:5]
        valid = i in (0, 1) and a != 0 and self.blocks[i]['g'] == a and self.blocks[i]['phase'] != 0
        b = self.blocks[i] if i in (0, 1) else None
        status = 0
        if op == 'open':
            status = int(not self.opened and all(x['phase'] == 0 for x in self.blocks))
            if status: self.opened = True
        elif op == 'end':
            self.opened = False
            for block in self.blocks:
                block['notice'] = 0
                if block['phase'] == 1: block['cancel'] = 1
        elif op == 'reserve':
            free = [j for j in (self.next, self.next ^ 1) if self.blocks[j]['phase'] == 0]
            if not self.opened: status = 1
            elif not free: status = 2
            elif self.generation == self.maximum: status = 3
            else:
                j = free[0]
                self.generation += 1
                self.blocks[j] = self.empty() | dict(g=self.generation, capture=a, phase=1)
                self.next = j ^ 1
                self.token = [self.generation, j]
        elif op == 'write':
            status = int(valid and b['phase'] == 1 and c == len(b['values']) and c < self.n)
            if status: b['values'].append(d & 65535)
        elif op == 'cancel':
            status = int(valid and b['phase'] == 1)
            if status: b['cancel'] = 1
        elif op == 'finish':
            allowed = valid and b['phase'] == 1 and e != 0 and c in (0, 1, 2) and d <= len(b['values'])
            status = int(bool(allowed and (c != 0 or d == self.n) and (c != 2 or b['cancel'])))
            if status:
                b.update(phase=2, count=d, result=c, notice=int(self.opened))
                self.ready.append(i)
        elif op == 'notice':
            pending = [j for j in self.ready if self.blocks[j]['notice']] if self.opened else []
            status = int(bool(pending))
            if status: self.token = [self.blocks[pending[0]]['g'], pending[0]]
        elif op == 'ack':
            status = int(bool(self.opened and valid and b['phase'] == 2 and b['notice']))
            if status: b['notice'] = 0
        elif op == 'take':
            status = int(bool(self.ready))
            if status:
                j = self.ready.pop(0)
                b = self.blocks[j]
                b.update(phase=3, notice=0)
                self.completion = [b['g'], j, b['capture'], b['count'], b['result']]
        elif op == 'sample':
            status = int(valid and b['phase'] == 3 and c < b['count'])
            if status: self.sample = b['values'][c]
        elif op == 'release':
            status = int(valid and b['phase'] == 3)
            if status: b['phase'] = 0
        else: raise ValueError(command)
        return copy.deepcopy(dict(status=status, token=self.token, completion=self.completion,
                                  sample=self.sample, blocks=[[
                                      b['g'], b['capture'], len(b['values']), b['count'], b['phase'],
                                      b['result'], b['cancel'], b['notice'],
                                      [b['values'][j] if b['phase'] == 3 and j < b['count'] else -1 for j in range(self.n)]
                                  ] for b in self.blocks]))
