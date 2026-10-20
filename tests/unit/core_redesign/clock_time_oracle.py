"""Independent register script and arithmetic reference for clock/time probes."""
RCC,CFG,FLASH,TRIM=0x40021000,0x40021004,0x40022000,0x1ffff7d4
CTL,SR,LOW,HIGH,CMPLO,CMPHI=0xe000f000,0xe000f004,0xe000f008,0xe000f00c,0xe000f010,0xe000f014


class Model:
    def __init__(self,wide):
        self.wide=wide;self.registers={RCC:1,CFG:0,FLASH:0,TRIM:255,CTL:0,SR:0,CMPLO:0,CMPHI:0}
        self.stage=0;self.open=False;self.generation=self.hz=self.ticks=self.jump=0
        self.low=0x12345678;self.sample=0x1122334455667788

    def apply(self,line):
        op,*args=line.split();a=list(map(int,args));r=self.registers;events=[];status=99
        def read(p):
            if p in [LOW,HIGH]:
                value=(self.ticks>>(32 if p==HIGH else 0))&0xffffffff
                self.ticks=(self.ticks+self.jump)&((1<<(64 if self.wide else 32))-1)
            else:value=r[p]
            events.extend([2 if p==TRIM else 0,p,value]);return value
        def write(p,v):
            v&=0xffffffff;events.extend([1,p,v])
            if p==LOW:self.ticks=(self.ticks&0xffffffff00000000)|v
            elif p==HIGH:self.ticks=(v<<32)|(self.ticks&0xffffffff)
            else:r[p]=v
        if op=="hardware":
            for p,value in zip([RCC,CFG,FLASH,TRIM],a):r[p]=value&(255 if p==TRIM else 0xffffffff)
        elif op=="clock_begin":
            if self.stage:status=1
            elif not a[0]:status=2
            else:
                control=read(RCC)
                if not self.wide and ((control&0x3000000) or read(CFG)&15):status=3
                else:write(RCC,control|1);self.stage=1;status=0
        elif op=="clock_step":
            if self.stage==1 and read(RCC)&2:
                write(FLASH,(read(FLASH)&~3)|(2 if self.wide else 1))
                if self.wide:write(CFG,read(CFG)&~0xf0);self.stage=3
                else:
                    trim=read(TRIM)
                    if trim!=255:write(RCC,(read(RCC)&~0xf8)|((trim&31)<<3))
                    write(CFG,read(CFG)&~0x100f0);write(RCC,read(RCC)|0x1000000);self.stage=2
            elif self.stage==2 and read(RCC)&0x2000000:
                write(CFG,(read(CFG)&~3)|2);self.stage=3
            elif self.stage==3:
                cfg,control,flash=read(CFG),read(RCC),read(FLASH)
                ready=control&3==3 and not cfg&0xf0 and flash&3==(2 if self.wide else 1)
                if not self.wide:ready=ready and control&0x3000000==0x3000000 and cfg&15==10
                if ready:self.stage=4
            status=self.stage
        elif op=="clock_abandon":
            status=int(self.stage in [1,2,3])
            if status:self.stage=5
        elif op=="counter_begin":
            frequency,owned,div=a
            if self.open:status=1
            elif not owned:status=2
            elif not frequency or (div and (frequency<8 or frequency%8)):status=3
            elif self.generation==0xffffffff:status=5
            elif read(CTL)!=0:status=4
            else:
                sequence=[(CTL,0),(SR,0),(LOW,0)]
                if self.wide:sequence.append((HIGH,0))
                sequence.append((CMPLO,0xffffffff))
                if self.wide:sequence.append((CMPHI,0xffffffff))
                sequence.append((CTL,1 if div else 5))
                for p,v in sequence:write(p,v)
                self.generation+=1;self.hz=frequency//(8 if div else 1);self.open=True;status=0
        elif op=="stop":
            status=int(self.open)
            if self.open:write(CTL,0);self.open=False
        elif op=="counter_control":r[CTL]=a[0]
        elif op=="ticks":self.ticks=a[0]&((1<<(64 if self.wide else 32))-1);self.jump=a[1]
        elif op in ["low","sample"]:
            if not self.open:status=1
            elif a[0]!=self.generation:status=2
            elif op=="low":self.low=read(LOW);status=0
            elif not self.wide:self.sample=read(LOW);status=0
            else:
                status=3
                for _ in range(min(a[1],3)):
                    high,low,verify=read(HIGH),read(LOW),read(HIGH)
                    if high==verify:self.sample=(high<<32)|low;status=0;break
        elif op in ["convert_constant","convert_specialized"]:
            status=0;self.sample=(a[0]*1000000)//6000000
        else:
            assert op=="convert"
            status=0 if a[1] else 1
            if a[1]:self.sample=(a[0]*1000000)//a[1]
        return [status,self.stage,self.generation,self.hz,self.low,self.sample,r[RCC],r[CFG],r[FLASH],r[CTL],r[SR],self.ticks,r[CMPLO],r[CMPHI],*events]
