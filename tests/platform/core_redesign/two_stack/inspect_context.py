"""Bounded integer instruction model of the exact swap bytes, not an MCU emulator."""
import hashlib
import random

SAVED=[1,2,8,9,*range(18,28)]


def decode(code):
    result=[];offset=0
    while offset<len(code):
        half=int.from_bytes(code[offset:offset+2],'little')
        if half&3!=3:
            length=2;op=half&3;kind=half>>13
            if op==0 and kind in (2,6):
                base=8+((half>>7)&7);register=8+((half>>2)&7)
                displacement=((half>>10)&7)*8+((half>>6)&1)*4+((half>>5)&1)*64
                instruction=('load' if kind==2 else 'store',register,base,displacement)
            else:
                assert half==0x8082,hex(half)
                instruction=('return',1,0,0)
        else:
            length=4;word=int.from_bytes(code[offset:offset+4],'little');op=word&127
            if op==0x23 and (word>>12)&7==2:
                displacement=((word>>25)<<5)|((word>>7)&31)
                if displacement&2048:displacement-=4096
                instruction=('store',(word>>20)&31,(word>>15)&31,displacement)
            elif op==3 and (word>>12)&7==2:
                displacement=word>>20
                if displacement&2048:displacement-=4096
                instruction=('load',(word>>7)&31,(word>>15)&31,displacement)
            else:
                assert word==0x8067,hex(word)
                instruction=('return',1,0,0)
        result.append((offset,length,*instruction));offset+=length
    assert result[-1][2]=='return' and len(result)==29
    return result


def verify(code,seeds=1000):
    program=decode(code)
    assert [(p[2],p[3],p[4],p[5]) for p in program[:-1]]==[
        *(('store',r,10,4*i) for i,r in enumerate(SAVED)),
        *(('load',r,11,4*i) for i,r in enumerate(SAVED))]
    for seed in range(seeds):
        rng=random.Random(seed);registers=[rng.randrange(2**32) for _ in range(32)];registers[0]=0
        registers[1]=0x1000;registers[2]=0x20003000;registers[10]=0x20000100;registers[11]=0x20000200
        original=registers.copy();memory={address:0xa55aa55a for address in range(0x200000fc,0x20000244,4)}
        next_values=[rng.randrange(2**32) for _ in SAVED];next_values[0]=0x2000;next_values[1]=0x20004000
        for i,value in enumerate(next_values):memory[0x20000200+4*i]=value
        def execute():
            for _,_,op,register,base,displacement in program:
                if op=='return':return registers[1]&~1
                address=(registers[base]+displacement)&0xffffffff
                assert address%4==0 and address in memory
                if op=='store':memory[address]=registers[register]
                else:registers[register]=memory[address]
            raise AssertionError('no return')
        assert execute()==0x2000
        assert [registers[r] for r in SAVED]==next_values
        assert registers[3:5]==original[3:5] and registers[2]%16==0
        assert [memory[0x20000100+4*i] for i in range(14)]==[original[r] for r in SAVED]
        # Ordinary calls on the other stack can change their callee registers.
        changed=[rng.randrange(2**32) for _ in SAVED];changed[0]=0x3000;changed[1]=0x20003f00
        for r,value in zip(SAVED,changed):registers[r]=value
        registers[10],registers[11]=0x20000200,0x20000100
        assert execute()==0x1000 and [registers[r] for r in SAVED]==[original[r] for r in SAVED]
        assert [memory[0x20000200+4*i] for i in range(14)]==changed
        for address in [0x200000fc,0x20000138,0x2000013c,0x20000140,0x200001fc,0x20000238,0x2000023c,0x20000240]:
            assert memory[address]==0xa55aa55a
    # A wrong restore offset must be rejected, not merely parse successfully.
    last=next(p for p in program if p[2:4]==('load',27));assert last[1]==4
    bad=bytearray(code);word=int.from_bytes(bad[last[0]:last[0]+4],'little');word=(word&0xfffff)|(48<<20)
    bad[last[0]:last[0]+4]=word.to_bytes(4,'little')
    try:
        decoded=decode(bad)
        assert [(p[3],p[5]) for p in decoded if p[2]=='load']==[(r,4*i) for i,r in enumerate(SAVED)]
    except AssertionError:pass
    else:raise AssertionError('wrong restore offset not rejected')
    return dict(scope='Exact swap byte integer model only; no traps, hardware IRQ, pipeline or elapsed cycles',
                sha256=hashlib.sha256(code).hexdigest(),code_bytes=len(code),instructions=len(program),
                seeds=seeds,swaps=seeds*2,wrong_restore_offset_rejected=True)
