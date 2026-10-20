"""ELF/map placement checks, not startup, upload or memory-coherence proofs."""
import re
import struct


def decode(raw, nm, map_text):
    if len(raw) < 52 or raw[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('ELF32 little-endian required')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', raw)
    if h[2] != 243 or h[9] != 32 or h[11] != 40:
        raise ValueError('RISC-V ELF32 header required')
    if h[6]+h[12]*40 > len(raw) or h[5]+h[10]*32 > len(raw) or h[13]>=h[12]:
        raise ValueError('ELF tables outside file')
    headers=[struct.unpack_from('<10I',raw,h[6]+i*40) for i in range(h[12])]
    strings=headers[h[13]]
    if strings[4]+strings[5]>len(raw): raise ValueError('section names outside file')
    names=raw[strings[4]:strings[4]+strings[5]]
    sections={}
    for s in headers[1:]:
        if s[0]>=len(names) or b'\0' not in names[s[0]:]: raise ValueError('invalid section name')
        name=names[s[0]:].split(b'\0',1)[0].decode('ascii')
        if name in sections: raise ValueError('duplicate section name')
        if s[8] and s[8] & (s[8]-1): raise ValueError('invalid section alignment')
        if s[1]!=8 and s[4]+s[5]>len(raw): raise ValueError('section outside file')
        sections[name]=dict(type=s[1],flags=s[2],address=s[3],offset=s[4],size=s[5],alignment=s[8])
    segments=[]
    for i in range(h[10]):
        p=struct.unpack_from('<8I',raw,h[5]+i*32)
        if p[0]!=1: continue
        if p[4]>p[5] or p[1]+p[4]>len(raw): raise ValueError('invalid load segment')
        segments.append(dict(offset=p[1],vaddr=p[2],paddr=p[3],file_bytes=p[4],memory_bytes=p[5],flags=p[6]))
    symbols={}
    for line in nm.splitlines():
        fields=line.split()
        if len(fields)==4 and re.fullmatch('[0-9a-fA-F]+',fields[0]):
            symbols[fields[3]]=dict(address=int(fields[0],16),size=int(fields[1],16))
        elif len(fields)==3 and re.fullmatch('[0-9a-fA-F]+',fields[0]):
            symbols[fields[2]]=dict(address=int(fields[0],16),size=0)
    regions={m[1]:[int(m[2],16),int(m[3],16)] for m in re.finditer(
        r'^([A-Z][A-Z_]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+[rwx]+',map_text,re.M)}
    return dict(entry=h[4],flags=h[7],sections=sections,segments=segments,symbols=symbols,regions=regions)


def overlap(a,b):
    return a[1]>0 and b[1]>0 and max(a[0],b[0])<min(a[0]+a[1],b[0]+b[1])


def inside(span,region):
    return span[1]>=0 and region[0]<=span[0] and span[0]+span[1]<=region[0]+region[1]


def validate_pair(images,config,shared_bytes):
    def require(ok,message):
        if not ok: raise ValueError(message)
    require(set(images)==set(config['cores']),'core set mismatch')
    shared_region=[config['shared_origin'],config['shared_capacity']]
    reserved=[];occupied=[];layout=[]
    for core,image in images.items():
        regions=config['cores'][core]['regions'];sections=image['sections'];symbols=image['symbols']
        require(image['regions']==regions,f'{core}: MEMORY reservation mismatch')
        require(image['flags']==config['elf_flags'],f'{core}: unexpected ELF ABI flags')
        for name,region in regions.items(): reserved.append((core,name,region))
        def symbol(name):
            require(name in symbols,f'{core}: missing symbol {name}')
            return symbols[name]['address']
        def section(name):
            require(name in sections,f'{core}: missing section {name}')
            return sections[name]
        shared=section('.shared');span=[shared['address'],shared['size']]
        require(shared['type']==8 and shared['flags']&7==3,f'{core}: shared must be writable NOLOAD')
        require(span==[config['shared_origin'],shared_bytes] and inside(span,shared_region),f'{core}: shared address/size mismatch')
        require(shared['alignment']>=config['alignment'] and
                not shared['alignment'] & (shared['alignment']-1) and
                span[0]%config['alignment']==0,f'{core}: shared alignment')
        require(symbol('experiment_shared')==span[0] and symbols['experiment_shared']['size']==shared_bytes,f'{core}: shared symbol shape')
        for name in ['.init_array','.preinit_array','.ctors']:
            require(not sections.get(name,{}).get('size',0),f'{core}: constructor array in probe')
        init=section('.init')
        require(image['entry']==symbol('_start') and inside([image['entry'],1],[init['address'],init['size']]),f'{core}: entry mismatch')
        for name in ['.data','.bss','.stack']:
            s=section(name)
            require(inside([s['address'],s['size']],regions['RAM']),f'{core}: {name} outside private RAM')
        data=section('.data');bss=section('.bss');stack=section('.stack')
        require([symbol('_data_vma'),symbol('_edata')]==[data['address'],data['address']+data['size']],f'{core}: data init bounds')
        require([symbol('_sbss'),symbol('_ebss')]==[bss['address'],bss['address']+bss['size']],f'{core}: bss clear bounds')
        require([symbol('_susrstack'),symbol('_eusrstack')]==[stack['address'],stack['address']+stack['size']],f'{core}: stack bounds')
        require(stack['size']==config['stack_bytes'] and stack['address']+stack['size']==sum(regions['RAM']),f'{core}: stack reservation')
        require(symbol('_ebss')<=symbol('_susrstack'),f'{core}: static/stack collision')
        loads=[]
        for p in image['segments']:
            if p['file_bytes']:
                require(inside([p['paddr'],p['file_bytes']],regions['FLASH']),f'{core}: load bytes outside own Flash')
                loads.append(p)
        data_load=[p['paddr']+data['address']-p['vaddr'] for p in loads
                   if inside([data['address'],data['size']],[p['vaddr'],p['file_bytes']])]
        require(data_load==[symbol('_data_lma')],f'{core}: data load bounds')
        for name,s in sections.items():
            if not s['size'] or not s['flags']&2: continue
            area=[s['address'],s['size']]
            require(any(inside(area,r) for r in regions.values()),f'{core}: unmapped allocated section {name}')
            if name!='.shared':
                require(not overlap(area,shared_region),f'{core}: private section in shared reservation')
                occupied.append((core,name,area))
        layout.append(dict(core=core,shared_address=span[0],shared_bytes=span[1],
                           bss_bytes=bss['size'],data_bytes=data['size'],stack_bytes=stack['size'],
                           static_to_stack_gap=stack['address']-(bss['address']+bss['size'])))
    require(len({v['flags'] for v in images.values()})==1,'ELF ABI flags disagree')
    for i,(ca,na,ra) in enumerate(reserved):
        for cb,nb,rb in reserved[i+1:]:
            if ca!=cb and na==nb=='RAM_SHARED': require(ra==rb,'shared reservations disagree')
            else: require(not overlap(ra,rb),f'reserved regions overlap: {ca}/{na}, {cb}/{nb}')
    for i,(ca,na,ra) in enumerate(occupied):
        for cb,nb,rb in occupied[i+1:]:
            require(not overlap(ra,rb),f'allocated sections overlap: {ca}/{na}, {cb}/{nb}')
    return layout
