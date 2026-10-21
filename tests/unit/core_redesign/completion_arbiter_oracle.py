"""Independent serial reservation and round-robin ledger, list representation."""
import copy


class Model:
    def __init__(self,dedicated,short=False):
        self.dedicated=dedicated;self.count=3 if dedicated else 2;self.limit=255 if short else 0xffffffff
        self.opened=[False]*3;self.records=[dict(phase=0,client=0,generation=0,pending=False,cancel=False,open=False) for _ in range(self.count)]
        self.buffers=[[0]*32 for _ in range(3)];self.loans={};self.cursor=0;self.current=self.count
        self.output=[99]*3;self.completion=[2,99,99];self.byte=99

    def find(self,t):
        g,i,c=t
        if i>=self.count or c>=3 or not g:return None
        r=self.records[i]
        return r if r['phase'] and r['generation']==g and r['client']==c else None

    def active(self,c):
        return next((r for r in self.records if r['phase'] and r['client']==c),None)

    def terminal(self,r,result,n,fill):
        data=self.buffers[r['buffer']]
        if r['mode']==1:
            for i in range(n):data[i]=(fill+i)&255
        h=2166136261
        for v in data[:n]:h=((h^v)*16777619)&0xffffffff
        r.update(phase=3,pending=r['open'],result=[result,n,h])

    def apply(self,line):
        fields=line.split();op=fields[0];a=list(map(int,fields[1:]));status=0
        if op=='open':
            c=a[0]
            if c<3 and not self.opened[c] and not self.active(c):self.opened[c]=True;status=1
        elif op=='end':
            c=a[0]
            if c<3 and self.opened[c]:
                self.opened[c]=False;status=1;r=self.active(c)
                if r:
                    r['open']=r['pending']=False
                    if r['phase']!=3:r['cancel']=True
                    if r['phase']==1:self.terminal(r,3,0,0)
        elif op=='submit':
            c,b,n,mode,address=a
            if c>=3:status=2
            elif not self.opened[c]:status=4
            elif self.active(c):status=1
            else:
                possible=[i for i,r in enumerate(self.records) if (not self.dedicated or i==c) and r['generation']<self.limit]
                free=next((i for i in possible if not self.records[i]['phase']),None)
                if free is None:status=1 if possible else 5
                elif mode not in (0,1):status=3
                elif not n or n>32 or address>127:status=2
                elif b in self.loans:status=1
                else:
                    r=self.records[free];r.update(phase=1,client=c,generation=r['generation']+1,
                        pending=False,cancel=False,open=True,buffer=b,length=n,mode=mode,result=[0,0,0])
                    self.loans[b]=free;self.output=[r['generation'],free,c]
        elif op=='dispatch':
            if self.current==self.count:
                for c in [(self.cursor+offset)%3 for offset in range(3)]:
                    r=self.active(c)
                    if r and r['phase']==1:
                        i=next(i for i,x in enumerate(self.records) if x is r)
                        r['phase']=2;self.current=i;self.cursor=(c+1)%3;self.output=[r['generation'],i,c];status=1;break
        elif op in ('finish','cancel','notice','ack','take'):
            t=a[:3];r=self.find(t)
            if op=='cancel':
                status=3
                if r:
                    status=2 if r['phase']==3 else 1 if r['cancel'] else 0
                    if r['phase']!=3:r['cancel']=True
                    if r['phase']==1:self.terminal(r,3,0,0)
            elif op=='finish':
                result,n,fill,stopped=a[3:]
                if r and r['phase']==2 and self.current==t[1] and stopped and result in (1,2,3) and n<=r['length'] and (result!=1 or n==r['length']) and (result!=3 or r['cancel']):
                    self.terminal(r,result,n,fill);self.current=self.count;status=1
            elif op=='notice':status=int(bool(r and r['pending'] and r['open'] and r['phase']==3))
            elif op=='ack':
                if r and r['pending'] and r['open'] and r['phase']==3:r['pending']=False;status=1
            elif r and r['phase']==3:
                self.completion=list(r['result']);del self.loans[r['buffer']]
                r.update(phase=0,pending=False,cancel=False,open=False);status=1
        elif op=='write':
            b,i,value=a
            if b not in self.loans and i<32:self.buffers[b][i]=value&255;status=1
        elif op=='read':
            b,i=a
            if b not in self.loans and i<32:self.byte=self.buffers[b][i];status=1
        elif op!='snapshot':raise ValueError(op)
        return copy.deepcopy(dict(status=status,token=self.output,completion=self.completion,byte=self.byte,
            cursor=self.cursor,current=self.current,opened=list(map(int,self.opened)),
            records=[[r['phase'],r['client'],r['generation'],int(r['pending']),int(r['cancel'])] for r in self.records],
            buffers=[[-1]*32 if i in self.loans else b for i,b in enumerate(self.buffers)]))
