"""Independent whole-frame and transaction-ledger model for X0 wire composition.

Images come from the existing independent renderer oracle, not C++ chunk output.
Requests and replies are calculated from input and a ledger, not driver state.
"""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('wire_base', Path(__file__).with_name('app_oracle.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class Model(base.ApplicationOracle):
    def __init__(self):
        super().__init__(touch_period=20)
        self.touch = True
        self.source = (10000, 1000)
        self.words = [0, 0]
        self.measurement_data = [0]*8
        self.measure_current_ready = False
        self.chunk = None
        self.display_data_ready = False

    def apply(self, line):
        op, *args = line.split()
        args = list(map(int, args))
        if op == 'raw':
            self.source = tuple(v & 65535 for v in args)
        elif op == 'cancel':
            if self.current and self.current['outcome'] is None:
                self.current['outcome'] = False
        elif op == 'time':
            self.now = args[0]
            self.pd.apply(line)
            if self.current and self.current['outcome'] is None and self.current['due'] <= self.now:
                self.current['outcome'] = self.current['success']
                if self.current['owner'] == 2 and self.current['success']:
                    index = 0 if self.current['payload'] == bytes([2]) else 1
                    self.current['received_word'] = self.source[index]
        else:
            return super().apply(line)
        return self.observe()

    def step(self):
        if self.current and self.current['outcome'] is not None:
            transaction = self.current
            self.current = None
            transaction['returned'] = True
            owner, ok = transaction['owner'], transaction['outcome']
            if owner == 1:
                if not ok:
                    self.chunk = None
                    self.display_data_ready = False
                    self.frame = None
                    self.next_frame = self.now+100
                elif transaction['payload'][0] == 0:
                    self.display_data_ready = True
                else:
                    self.offset += len(self.chunk)
                    self.chunk = None
                    if self.offset == 1024:
                        self.displayed_revision = self.frame_revision
                        self.displayed_hash = base.fnv(self.frame)
                        self.frames += 1
                        self.frame = None
            elif owner == 2:
                index = 0 if transaction['payload'] == bytes([2]) else 1
                if ok:
                    self.words[index] = transaction['received_word']
                if ok and index == 0:
                    self.measure_current_ready = True
                else:
                    self.measure_current_ready = False
                    self.measure_done += 1
                    mv = (self.words[0]//8)*4
                    ma = self.words[1] if self.words[1] < 32768 else self.words[1]-65536
                    self.pd.apply(f'sample {int(ok)} {mv} {ma}')
                    if ok:
                        self.measurement_data = list(mv.to_bytes(4,'little',signed=True)+ma.to_bytes(4,'little',signed=True))
            elif owner == 3 and ok:
                self.touch_done += 1
        self.pd.apply('step')
        view = (self.pd.desired,self.pd.last_confirmed,self.pd.mv,self.pd.ma,
                self.pd.confirmed is not None,self.pd.stale,self.pd.online)
        if view != self.latest_view:
            self.latest_view = view
            self.revision += 1
        for owner, period, name in [(2,100,'measure'),(3,20,'touch')]:
            next_due = getattr(self,'next_'+name)
            if self.now >= next_due:
                count = (self.now-next_due)//period+1
                pending = getattr(self,name+'_pending')
                setattr(self,'missed_'+name,getattr(self,'missed_'+name)+count-1+int(pending))
                setattr(self,name+'_pending',True)
                setattr(self,'next_'+name,next_due+count*period)
        if self.frame is None and self.revision != self.displayed_revision and self.now >= self.next_frame:
            self.frame = base.render_frame(*self.latest_view)
            self.frame_revision = self.revision
            self.offset = 0
            self.next_frame = self.now+100
        if self.current is not None:
            return
        for delta in range(1,4):
            owner = (self.cursor+delta-1)%3+1
            address, rx_size, kind = 0, 0, 0
            if owner == 1:
                if self.frame is None:
                    continue
                address = 0x3c
                if self.display_data_ready:
                    payload = bytes([0x40])+self.chunk
                    self.display_data_ready = False
                else:
                    self.chunk = self.frame[self.offset:self.offset+32]
                    column, page = self.offset%128, self.offset//128
                    payload = bytes([0,0x20,0,0x21,column,column+len(self.chunk)-1,0x22,page,page])
                    if self.offset == 0:
                        self.next_frame = self.now+100
            elif owner == 2:
                if not self.measure_current_ready and not self.measure_pending:
                    continue
                payload = bytes([4 if self.measure_current_ready else 2])
                address, rx_size = 0x40, 2
                if self.measure_current_ready:
                    self.measure_current_ready = False
                else:
                    self.measure_pending = False
            else:
                if not self.touch_pending:
                    continue
                payload = (self.touch_done+1).to_bytes(4,'little')+bytes(4)
                self.touch_pending = False
                kind = 1
            self.cursor = owner
            self.current = dict(owner=owner,payload=payload,address=address,rx_size=rx_size,kind=kind,
                                due=self.now+2,success=owner not in self.fail_next,outcome=None,returned=False)
            self.fail_next.discard(owner)
            self.ledger.append(self.current)
            break

    def observe(self):
        row = super().observe()
        latest = self.ledger[-1] if self.ledger else None
        row.update(offset=self.offset,frame_revision=self.frame_revision,frame=int(self.frame is not None),
                   raw_words=list(self.words),measurement_data=list(self.measurement_data),
                   packet=dict(owner=latest['owner'] if latest else 0,kind=latest['kind'] if latest else 0,
                               address=latest['address'] if latest else 0,rx_size=latest['rx_size'] if latest else 0,
                               tx=list(latest['payload']) if latest else []))
        return row
