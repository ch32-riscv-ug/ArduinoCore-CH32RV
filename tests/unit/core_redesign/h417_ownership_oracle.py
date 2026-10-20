"""List-based, serial ownership ledger, independent of C++ ring layout."""
import copy


class Model:
    def __init__(self, mode, domain=7, short=False):
        self.mode, self.domain, self.limit = mode, domain, 255 if short else 2**32-1
        self.epoch, self.generation = 1, 0
        self.recovering, self.quiet, self.notice, self.blocked = False, 0, 0, False
        self.messages, self.ready = [], []
        self.blocks = [dict(phase=0, owner=0, generation=0, data=[]) for _ in range(2)]
        self.token, self.message, self.byte = [99]*4, [99]*4, 999

    def producer(self, core):
        return core in (0, 1) and (self.mode == 2 or core == 0)

    def consumer(self, core):
        return core in (0, 1) and (self.mode == 2 or core == (0 if self.mode == 0 else 1))

    def find(self, token):
        d, e, g, i = token
        if d != self.domain or e != self.epoch or not g or i >= 2:
            return None
        b = self.blocks[i]
        return b if b['phase'] and b['generation'] == g else None

    def apply(self, command):
        parts = command.split()
        op, args = parts[0], list(map(int, parts[1:]))
        core = args[0] if args else 0
        active = bool(self.domain and not self.recovering)
        ok = False
        if op == 'gate':
            self.blocked, ok = bool(core), True
        elif op == 'snapshot':
            ok = True
        elif not self.blocked:
            if op == 'recover':
                if self.domain and core == 0 and not self.recovering:
                    self.recovering, self.quiet, ok = True, 0, True
            elif op == 'attest':
                if core == 0 and self.recovering and 0 < args[1] <= 7:
                    self.quiet |= args[1]
                    ok = True
            elif op == 'restart':
                if core == 0 and self.recovering and self.quiet == 7 and self.epoch < self.limit:
                    self.epoch += 1
                    self.messages.clear(); self.ready.clear()
                    for b in self.blocks:
                        b['phase'] = 0
                    self.recovering, self.quiet, self.notice, ok = False, 0, 0, True
            elif active:
                if op == 'push' and self.producer(core) and len(self.messages) < 4:
                    self.messages.append(args[1:]); self.notice |= 1; ok = True
                elif op == 'pop' and self.consumer(core) and self.messages:
                    self.message = self.messages.pop(0); ok = True
                elif op == 'reserve' and self.producer(core) and self.generation < self.limit:
                    free = [i for i, b in enumerate(self.blocks) if not b['phase']]
                    if free:
                        i = free[0]; b = self.blocks[i]; self.generation += 1
                        b.update(phase=1, owner=core, generation=self.generation, data=[])
                        self.token = [self.domain, self.epoch, self.generation, i]; ok = True
                elif op == 'take' and self.consumer(core) and self.ready:
                    i = self.ready.pop(0); b = self.blocks[i]
                    b.update(phase=3, owner=core)
                    self.token = [self.domain, self.epoch, b['generation'], i]; ok = True
                elif op == 'ack' and self.consumer(core) and args[1] <= 3:
                    self.notice &= ~args[1]; ok = True
                elif op in ('write', 'publish', 'read', 'release'):
                    b = self.find(args[1:5])
                    if b and b['owner'] == core:
                        if op == 'write' and self.producer(core) and b['phase'] == 1 and args[5] == len(b['data']) and args[5] < 64 and args[6] <= 255:
                            b['data'].append(args[6]); ok = True
                        elif op == 'publish' and self.producer(core) and b['phase'] == 1 and len(b['data']) == 64:
                            b['phase'] = 2; self.ready.append(args[4]); self.notice |= 2; ok = True
                        elif op == 'read' and self.consumer(core) and b['phase'] == 3 and args[5] < 64:
                            self.byte = b['data'][args[5]]; ok = True
                        elif op == 'release' and self.consumer(core) and b['phase'] == 3:
                            b['phase'] = 0; ok = True
        blocks = []
        for b in self.blocks:
            h = 2166136261
            for byte in b['data']:
                h = ((h ^ byte)*16777619) & 0xffffffff
            blocks.append([b['phase'], b['owner'], b['generation'], len(b['data']), h])
        return copy.deepcopy(dict(ok=int(ok), token=self.token, message=self.message, byte=self.byte,
            state=[self.domain, self.epoch, self.generation, int(self.recovering), self.quiet,
                   self.notice, len(self.messages), len(self.ready)], messages=self.messages, blocks=blocks))
