"""Unbounded integer/rational clock and absolute-deadline reference; no MMIO."""
from fractions import Fraction

MAX=(1<<64)-1
HALF=1<<31
MOD=1<<32


class Timer:
    def __init__(self,relaxed):
        self.relaxed=relaxed;self.active=False;self.pending=False;self.bound=False
        self.epoch=0;self.last=0;self.next=0;self.period=0;self.due=0;self.skipped=0;self.saturated=False
    def trusted(self,stamp):return self.relaxed or not stamp[2]&3
    def arm(self,status,stamp,delay,period):
        if status not in [0,1] or not stamp[1] or not self.trusted(stamp) or delay>=HALF or period>=HALF:return 0
        self.last=stamp[0];self.epoch=stamp[1];self.next=self.last+delay;self.period=period
        self.bound=self.active=True;self.pending=False;self.due=self.skipped=0;self.saturated=False;return 1
    def poll(self,status,stamp):
        if status not in [0,1]:return 2
        if not self.bound:return 4
        if stamp[1]!=self.epoch:self.active=self.bound=False;return 5
        if not self.trusted(stamp):return 3
        if stamp[0]<self.last:self.active=self.bound=False;return 6
        if stamp[0]-self.last>=HALF:self.active=self.bound=False;return 7
        self.last=stamp[0]
        if not self.active or self.last<self.next:return 1
        count=(self.last-self.next)//self.period+1 if self.period else 1
        latest=self.next+(count-1)*self.period
        skipped=self.skipped+count if self.pending else count-1
        self.saturated=(self.saturated if self.pending else False) or skipped>MOD-1
        self.skipped=min(skipped,MOD-1);self.due=latest%MOD;self.pending=True
        if self.period:self.next=latest+self.period
        else:self.active=False
        return 0
    def take(self):
        if not self.pending:return None
        self.pending=False;return [self.due,self.skipped,int(self.saturated),self.epoch]
    def view(self):return [int(self.active),int(self.pending),self.epoch]


class Model:
    def __init__(self,variant,short=False):
        self.bits=variant['bits'];self.assumed=variant['assumed'];self.fixed=variant['fixed_six'];self.max_epoch=255 if short else MOD-1
        self.phase=0;self.time=Fraction(0);self.raw=0;self.epoch=0;self.source=0;self.hz=6000000 if self.fixed else 0;self.quality=0
        self.out=[123,77,99];self.drop=[55,66];self.notice=[77,78,1,79];self.last=3
        self.timers=[Timer(False),Timer(True)]
    def valid(self,source,raw,hz):return source>0 and hz>0 and (not self.fixed or hz==6000000) and (self.bits==64 or raw<MOD)
    def stamp(self):
        self.out=[int(self.time),self.epoch,self.quality];return 1 if self.quality&1 else 0
    def apply(self,line):
        op,*values=line.split();v=list(map(int,values));status=99
        if op=='begin':
            source,raw,hz,anchor=v
            if self.phase in [1,2]:status=2
            elif not self.valid(source,raw,hz):status=5
            elif self.epoch==self.max_epoch:status=13
            else:
                self.epoch+=1;self.source=source;self.raw=raw;self.hz=hz;self.time=Fraction(anchor);self.quality=0;self.phase=1;status=self.stamp()
            self.last=status
        elif op=='observe':
            source,raw,hz,known,bound=v
            if self.phase==2:status=4
            elif self.phase!=1:status=3
            elif not self.valid(source,raw,hz):status=5
            elif source!=self.source:status=6
            elif hz!=self.hz:status=7
            else:
                delta=raw-self.raw
                if self.bits==32 and delta<0:delta+=MOD
                if self.bits==64 and delta<0:status=11
                elif self.bits==32 and known and bound>=MOD:status=9
                elif known and delta>bound:status=10
                elif self.bits==32 and not known and not self.assumed:status=8
                else:
                    updated=self.time+Fraction(delta*1000000,hz)
                    if int(updated)>MAX:status=12
                    else:
                        self.time=updated;self.raw=raw
                        if self.bits==32 and not known:self.quality|=1
                        status=self.stamp()
            self.last=status
        elif op=='pause':
            status=int(self.phase==1)
            if status:self.phase=2
        elif op=='stop':
            status=int(self.phase not in [0,3])
            if status:self.phase=3
        elif op=='known':
            source,raw,hz,gap,allow=v;remainder=self.time-int(self.time);loses=hz!=self.hz and bool(remainder)
            if self.phase!=2:status=3
            elif not self.valid(source,raw,hz):status=5
            elif loses and not allow:status=14
            elif int(self.time)+gap>MAX:status=12
            else:
                denominator=6 if self.fixed else self.hz
                self.drop=[int(remainder*denominator) if loses else 0,denominator]
                if hz!=self.hz:self.time=Fraction(int(self.time))
                if loses:self.quality|=2
                self.time+=gap;self.source=source;self.raw=raw;self.hz=hz;self.phase=1;status=self.stamp()
            self.last=status
        elif op=='unknown':
            source,raw,hz,anchor=v
            if self.phase!=2:status=3
            elif not self.valid(source,raw,hz):status=5
            elif self.epoch==self.max_epoch:status=13
            else:
                self.epoch+=1;self.source=source;self.raw=raw;self.hz=hz;self.time=Fraction(anchor);self.quality=0;self.phase=1;status=self.stamp()
            self.last=status
        elif op=='arm':status=self.timers[v[0]].arm(self.last,self.out,v[1],v[2])
        elif op=='poll':status=self.timers[v[0]].poll(self.last,self.out)
        elif op=='take':
            notice=self.timers[v[0]].take();status=int(notice is not None)
            if notice is not None:self.notice=notice
        elif op=='stamp':
            self.out=v[:3];self.last=v[3]
        else:assert op=='state'
        denominator=6 if self.fixed else self.hz
        return dict(status=status,last=self.last,phase=self.phase,us=int(self.time),raw=self.raw,epoch=self.epoch,source=self.source,hz=self.hz,
                    fraction=int((self.time-int(self.time))*denominator),denominator=denominator,quality=self.quality,out=list(self.out),drop=list(self.drop),
                    strict=self.timers[0].view(),relaxed=self.timers[1].view(),notice=list(self.notice))
