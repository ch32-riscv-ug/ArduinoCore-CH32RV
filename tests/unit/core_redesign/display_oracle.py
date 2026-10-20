"""Whole-image/transaction-ledger reference, separate from chunk rendering."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('display_application_reference',Path(__file__).with_name('app_oracle.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def image(panel, view):
    mono = base.render_frame(*view)
    if panel in (0, 2): return mono
    if panel == 1:
        compact = bytearray(mono[:512])
        compact[32:56] = mono[640:664]
        return bytes(compact)
    pixels = bytearray()
    for y in range(64):
        for x in range(96):
            lit = bool(mono[(y//8)*128+x] & (1 << (y%8)))
            pixels.extend((0x07e0 if lit else 0).to_bytes(2,'little'))
    return bytes(pixels)


class Model(base.ApplicationOracle):
    def __init__(self, panel):
        super().__init__(touch_period=20)
        self.panel = panel
        self.touch = True
        self.chunk = [32,31,16,64][panel]
        self.frame_bytes = [1024,512,1024,12288][panel]

    def step(self):
        if self.current and self.current['outcome'] is not None:
            transaction = self.current
            self.current = None
            transaction['returned'] = True
            owner, ok = transaction['owner'], transaction['outcome']
            if owner == 1:
                if not ok:
                    self.frame = None
                    self.next_frame = self.now+100
                else:
                    self.offset += len(transaction['payload'])
                    if self.offset == self.frame_bytes:
                        self.displayed_revision = self.frame_revision
                        self.displayed_hash = base.fnv(self.frame)
                        self.frames += 1
                        self.frame = None
            elif owner == 2:
                self.measure_done += 1
                mv, ma = transaction['sample']
                self.pd.apply(f'sample {int(ok)} {mv} {ma}')
            elif owner == 3 and ok: self.touch_done += 1
        self.pd.apply('step')
        view = (self.pd.desired,self.pd.last_confirmed,self.pd.mv,self.pd.ma,
                self.pd.confirmed is not None,self.pd.stale,self.pd.online)
        if view != self.latest_view:
            self.latest_view = view
            self.revision += 1
        for owner, period, name in [(2,100,'measure'),(3,20,'touch')]:
            next_due = getattr(self,'next_'+name)
            if self.now >= next_due:
                deadlines = (self.now-next_due)//period+1
                pending = getattr(self,name+'_pending')
                setattr(self,'missed_'+name,getattr(self,'missed_'+name)+deadlines-1+int(pending))
                setattr(self,name+'_pending',True)
                setattr(self,'next_'+name,next_due+period*deadlines)
        if self.frame is None and self.revision != self.displayed_revision and self.now >= self.next_frame:
            self.frame = image(self.panel,self.latest_view)
            self.frame_revision = self.revision
            self.offset = 0
            self.next_frame = self.now+100
        if self.current is not None: return
        ready = {1:self.frame is not None,2:self.measure_pending,3:self.touch_pending}
        for delta in range(1,4):
            owner = (self.cursor+delta-1)%3+1
            if not ready[owner]: continue
            self.cursor = owner
            if owner == 1:
                payload = self.frame[self.offset:self.offset+self.chunk]
                if self.offset == 0: self.next_frame = self.now+100
            elif owner == 2:
                payload = b''.join((v & 0xffffffff).to_bytes(4,'little') for v in self.source)
                self.measure_pending = False
            else:
                payload = (self.touch_done+1).to_bytes(4,'little')+bytes(4)
                self.touch_pending = False
            self.current = dict(owner=owner,payload=payload,sample=self.source,
                due=self.now+1+(len(payload)+15)//16,success=owner not in self.fail_next,outcome=None,returned=False)
            self.fail_next.discard(owner)
            self.ledger.append(self.current)
            break

    def observe(self):
        result = super().observe()
        latest = self.ledger[-1] if self.ledger else None
        result.update(offset=self.offset,frame_revision=self.frame_revision,frame=int(self.frame is not None),
                      length=len(latest['payload']) if latest else 0,sealed_hash=base.fnv(latest['payload']) if latest else 0)
        return result
