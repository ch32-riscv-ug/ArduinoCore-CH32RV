"""Bounded instruction-level startup harness using pinned objdump decoding.

Not an MCU emulator. Only startup executes; constructor calls are ABI hooks.
Peripheral, trap handling, hardware stack, caches and main are not executed.
"""
import re
import struct


def elf_sections(raw):
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', raw)
    assert h[0][:7] == b'\x7fELF\x01\x01\x01' and h[2] == 243
    headers = [struct.unpack_from('<10I', raw, h[6] + i * h[11]) for i in range(h[12])]
    shstr = headers[h[13]]; names = raw[shstr[4]:shstr[4] + shstr[5]]
    result = {}
    for s in headers:
        name = names[s[0]:].split(b'\0', 1)[0].decode()
        result[name] = dict(type=s[1], flags=s[2], address=s[3], size=s[5],
                            data=raw[s[4]:s[4]+s[5]] if s[1] != 8 else b'')
    return h, result


def symbols(text):
    result = {}
    for line in text.splitlines():
        f = line.split()
        if len(f) == 3 and re.fullmatch('[0-9a-fA-F]+', f[0]): result[f[2]] = int(f[0], 16)
    return result


def inspect(raw, image, disasm, nm, ram_bytes, x035):
    header, sections = elf_sections(raw); names = symbols(nm)
    assert header[4] == names['_start'] == 0
    assert bool(header[7] & 8) == (not x035)
    vector = names['experiment_vectors']; count = 55 if x035 else 39
    assert vector == (4 if x035 else 0)
    assert int.from_bytes(image[:4], 'little') & 0x7f == 0x6f
    assert sections['.reset']['size'] == (4 if x035 else 156)
    assert (sections['.vectors']['size'] if x035 else sections['.reset']['size']) == count * 4
    reserved = {1, 4, 6, 7, 10, 11, 13, 15} | ({19} if x035 else {5, 8, 9})
    for index in range(1, count):
        address = int.from_bytes(image[vector+4*index:vector+4*index+4], 'little')
        assert address == (0 if index in reserved else names['experiment_unhandled']), (index,address)
    if x035: assert int.from_bytes(image[vector:vector+4], 'little') == 0
    instructions = {}
    for line in disasm.splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+([0-9a-f]+)\s+([\w.]+)\s*(.*)', line)
        if match:
            address, word, op, tail = match.groups(); address = int(address,16)
            length = len(word)//2
            assert image[address:address+length] == int(word,16).to_bytes(length,'little')
            instructions[address] = (length, op, tail.split('#')[0].strip())
    memory = bytearray([0xa5])*ram_bytes; registers = [0]*32; csr = {0x300:0x1888,0x305:0}
    pc = 0; calls=[]; steps=0; writes=[]
    ctor_addresses=[]
    for start,end in [('__preinit_start','__preinit_end'),('__init_start','__init_end')]:
        ctor_addresses += [int.from_bytes(image[i:i+4],'little') for i in range(names[start],names[end],4)]
    def number(s): return int(s,0)
    def reg(s):
        assert re.fullmatch(r'x\d+',s),s
        i=int(s[1:]); assert i < (32 if x035 else 16); return i
    def load(address):
        assert address%4==0
        if 0 <= address <= len(image)-4:return int.from_bytes(image[address:address+4],'little')
        offset=address-0x20000000;assert 0<=offset<=ram_bytes-4
        return int.from_bytes(memory[offset:offset+4],'little')
    def initialized():
        expected=bytearray([0xa5])*ram_bytes
        a,b=names['__data_start']-0x20000000,names['__data_end']-0x20000000
        expected[a:b]=image[names['__data_load']:names['__data_load']+b-a]
        a,b=names['__bss_start']-0x20000000,names['__bss_end']-0x20000000
        expected[a:b]=bytes(b-a)
        assert memory==expected, 'data/bss or guard bytes differ'
    while pc != names['experiment_main']:
        steps+=1;assert steps<20000, 'startup did not reach main'
        if pc in ctor_addresses:
            initialized();calls.append(pc);return_pc=registers[1]
            # ABI hook only: deliberately clobber caller-saved registers.
            for i in [1,5,6,7,*range(10,16 if not x035 else 18),*range(28,32)]:
                registers[i]=0xdead0000+i
            pc=return_pc;continue
        assert pc in instructions,(hex(pc),'no decoded instruction')
        length,op,tail=instructions[pc];args=[x.strip() for x in tail.split(',')];next_pc=pc+length
        if op in ['auipc','lui','c.lui']:
            registers[reg(args[0])]=(pc if op=='auipc' else 0)+(number(args[1])<<12)
        elif op in ['addi','ori']:
            a,b,c=reg(args[0]),registers[reg(args[1])],number(args[2])
            registers[a]=b+c if op=='addi' else b|c
        elif op in ['c.addi','c.addi16sp','c.li']:
            a=reg(args[0]);registers[a]=(0 if op=='c.li' else registers[a])+number(args[1])
        elif op=='c.mv':registers[reg(args[0])]=registers[reg(args[1])]
        elif op in ['lw','sw','c.lw','c.sw','c.lwsp','c.swsp']:
            m=re.fullmatch(r'(-?\d+)\((x\d+)\)',args[1]);assert m,args
            address=(registers[reg(m[2])]+int(m[1]))&0xffffffff
            if op in ['lw','c.lw','c.lwsp']:registers[reg(args[0])]=load(address)
            else:
                offset=address-0x20000000;assert address%4==0 and 0<=offset<=ram_bytes-4
                memory[offset:offset+4]=(registers[reg(args[0])]&0xffffffff).to_bytes(4,'little');writes.append(address)
        elif op in ['bgeu','bltu']:
            a,b=registers[reg(args[0])],registers[reg(args[1])]
            if (a>=b if op=='bgeu' else a<b):next_pc=int(args[2].split()[0],16)
        elif op in ['jal','c.j','c.jal']:
            target=args[-1];rd=reg(args[0]) if op=='jal' else (1 if op=='c.jal' else 0)
            registers[rd]=next_pc;next_pc=int(target.split()[0],16)
        elif op in ['jalr','c.jr','c.jalr']:
            if op=='jalr':
                rd=reg(args[0]);m=re.fullmatch(r'(-?\d+)\((x\d+)\)',args[1]);assert m,args
                address=registers[reg(m[2])]+int(m[1])
            else:rd=1 if op=='c.jalr' else 0;address=registers[reg(args[0])]
            registers[rd]=next_pc;next_pc=address&~1
        elif op in ['csrrci','csrrw']:
            field={'mstatus':0x300,'mtvec':0x305}.get(args[1]);assert field is not None,args
            old=csr[field];csr[field]=old & ~number(args[2]) if op=='csrrci' else registers[reg(args[2])]
            registers[reg(args[0])]=old
        else:raise AssertionError(('unsupported startup instruction',hex(pc),op,tail))
        registers=[v&0xffffffff for v in registers];registers[0]=0;pc=next_pc
    initialized();assert calls==ctor_addresses
    assert not csr[0x300]&8 and csr[0x305]==vector|3
    assert registers[2]==names['__stack_top'] and registers[3]==names['__global_pointer$']
    static=names['__static_end']-0x20000000;stack=names['__stack_top']-names['__stack_bottom']
    return dict(entry=0,vector_address=vector,vector_slots=count,steps=steps,word_writes=len(writes),
        constructor_hooks=calls,data_bytes=names['__data_end']-names['__data_start'],
        bss_bytes=names['__bss_end']-names['__bss_start'],static_ram_span=static,reserved_stack=stack,
        flash_bytes=len(image),free_ram_between_static_and_stack=names['__stack_bottom']-names['__static_end'],
        boot='instruction harness with ABI ctor hooks; main/MCU not executed')
