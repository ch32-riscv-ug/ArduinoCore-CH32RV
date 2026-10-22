"""Connection ledger projection for the frozen whole-application oracle."""
import importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('connection_application',Path(__file__).with_name('app_oracle.py'))
application=importlib.util.module_from_spec(spec);spec.loader.exec_module(application)

class Peer(application.PpsOracle):
    def __init__(self,recover):
        super().__init__();self.recover=recover;self.port=False;self.auto=True;self.discarded=0
        self.payload=[0]*8;self.last=[0,0,0];self.status=99
    def finish(self,token,epoch,result,count,quiet):
        cur=self.current
        if not quiet or cur is None or cur['outcome'] is not None or (token,epoch)!=(cur['id'],cur['session']):return False
        if result not in [1,2,3] or count>8 or count<0 or (result==1 and count!=8) or (result==3 and not cur['cancelled']):return False
        cur['outcome']=result==1;cur['result']=result;cur['count']=count;return True
    def apply(self,line):
        op,*values=line.split();a=list(map(int,values));self.status=99
        if op=='attach':
            if self.online:self.status=1
            elif not self.recover and self.connection:self.status=2
            else:self.connection+=1;self.online=True;self.confirmed=self.selected=None;self.status=0
        elif op=='detach':
            self.online=False;self.confirmed=self.selected=None;self.port=False
            if self.current is not None and self.current['outcome'] is None:self.current['cancelled']=True
        elif op=='key':
            if not self.online or a[0] not in [-1,0,1] or len(self.commands)==4:self.rejected+=1;self.status=0
            else:self.commands.append((a[0],self.connection));self.accepted+=1;self.status=1
        elif op=='hold':self.auto=not bool(a[0])
        elif op=='terminal':self.status=int(self.finish(*a))
        elif op=='finish':self.status=int(self.finish(a[0],a[1],1 if a[2] else 2,8,True))
        elif op=='mutate':
            self.status=int(self.current is None and 0<=a[0]<8)
            if self.status:self.payload[a[0]]=a[1]&255
        elif op=='time':
            self.time=a[0]
            if self.auto and self.current and self.current['outcome'] is None and self.current['due']<=self.time:
                c=self.current;self.finish(c['id'],c['session'],3 if c['cancelled'] else 1,8,True)
        elif op=='step':
            if self.current is not None and self.current['outcome'] is not None:
                c=self.current
                if self.online and self.port and c['session']==self.connection and c['outcome']:self.confirmed=self.last_confirmed=c['value']
                c['returned']=True;self.last=[c['result'],c['count'],application.fnv(self.payload[:c['count']])];self.current=None
            pending=list(self.commands);self.commands.clear()
            for key,epoch in pending:
                if not self.online or (self.recover and epoch!=self.connection):self.discarded+=1;continue
                if key:self.desired=max(5000,min(11000,self.desired+20*key))
                else:self.selected=self.desired
            if self.online and self.current is None:
                self.port=True
                if self.selected is not None:
                    cur=dict(id=len(self.transactions)+1,session=self.connection,value=self.selected,outcome=None,due=self.time+5,returned=False,cancelled=False)
                    self.transactions.append(cur);self.current=cur;self.selected=None
                    self.payload=list(cur['value'].to_bytes(4,'little'))+list(cur['session'].to_bytes(4,'little'))
            self.status=0
        elif op=='inspect':pass
        else:super().apply(line)

class Model:
    def __init__(self,recover,touch,domain,opt):
        self.app=application.ApplicationOracle(touch_period=20);self.app.touch=touch
        self.peer=Peer(recover);self.app.pd=self.peer;self.domain=domain;self.opt=opt
    def apply(self,line):
        op=line.split()[0];status=99
        if op in ['attach','detach','key','finish','hold','terminal','mutate','inspect']:
            self.peer.apply(line);status=self.peer.status
        elif op in ['step','opportunity']:
            if op=='step':where,profile,context=0,0,0
            else:where,profile,context=map(int,line.split()[1:])
            allowed=context==0 and (where==0 or (profile!=0 and (self.domain==0 or (profile==2 and self.opt))))
            if allowed:self.app.apply('step')
            status=2 if context in [1,3] else 3 if context==2 else 4 if where!=0 and profile==0 else 0
        else:self.app.apply(line)
        p=self.peer;c=p.current
        return dict(application=self.app.observe(),status=status,connection=[p.connection,int(p.online),int(p.port),int(c is not None),int(c is not None and c['outcome'] is not None),int(c is not None and c['cancelled']),len(p.transactions),int(c is None),p.discarded,*p.last],payload=p.payload.copy() if c is None else [])
