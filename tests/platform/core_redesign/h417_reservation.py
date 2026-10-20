"""Derive link-only reservation variants without modifying the pinned SDK."""
import copy
import importlib.util
from pathlib import Path
import re

spec=importlib.util.spec_from_file_location('reservation_base_layout',Path(__file__).with_name('h417_link_layout.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)


def geometry(config,capacity):
    if type(capacity) is not int or not 0<=capacity<=config['shared_capacity'] or capacity%config['alignment']:
        raise ValueError('capacity must fit pinned shared allocation and alignment')
    result=copy.deepcopy(config)
    end=config['shared_origin']+config['shared_capacity']
    result['shared_origin']=end-capacity;result['shared_capacity']=capacity
    for core,settings in result['cores'].items():
        settings['regions']['RAM_SHARED']=[end-capacity,capacity]
        if core=='V3F':
            ram=settings['regions']['RAM']
            ram[1]=end-capacity-ram[0]
    return result


def derive(source,core,config,capacity,version=1):
    selected=geometry(config,capacity)
    if core not in selected['cores']:raise ValueError('unknown core')
    substitutions=[('ORIGIN = (0x20178000), LENGTH = 32K',
                    f'ORIGIN = 0x{selected["shared_origin"]:08x}, LENGTH = {capacity}')]
    if core=='V3F':substitutions.append(('LENGTH = (448K-256-32K)',f'LENGTH = (448K-256-{capacity})'))
    for old,new in substitutions:
        if source.count(old)!=1:raise ValueError('pinned linker shape changed')
        source=source.replace(old,new)
    if core=='V5F' and capacity==0:
        source,count=re.subn(r'INPUT\(\s*\.\./\.\./V3F/obj/User/shared\.o\s*\)',
                             '/* Shared object omitted in disabled experiment. */',source)
        if count!=1:raise ValueError('shared object INPUT shape changed')
    source+=f'''
/* Experiment configuration symbols: no storage is allocated for them. */
__experiment_shared_capacity = {capacity};
__experiment_shared_origin = 0x{selected['shared_origin']:08x};
__experiment_shared_version = {version};
ASSERT(SIZEOF(.shared) <= {capacity}, "experiment shared capacity exceeded")
'''
    return source


def validate(images,config,capacity,payload_bytes,version=1):
    selected=geometry(config,capacity)
    def require(condition,message):
        if not condition:raise ValueError(message)
    require(set(images)==set(selected['cores']),'core set mismatch')
    for core,image in images.items():
        for name,value in [('capacity',capacity),('origin',selected['shared_origin']),('version',version)]:
            symbol=image['symbols'].get('__experiment_shared_'+name)
            require(symbol is not None and symbol['address']==value,f'{core}: shared configuration mismatch')
    if capacity:
        return base.validate_pair(images,selected,payload_bytes)
    require(payload_bytes==0,'disabled reservation cannot contain payload')
    occupied=[]
    rows=[]
    for core,image in images.items():
        regions=selected['cores'][core]['regions'];s=image['sections'];n=image['symbols']
        require(image['regions']==regions,f'{core}: MEMORY reservation mismatch')
        require(image['flags']==selected['elf_flags'],f'{core}: ABI mismatch')
        require('experiment_shared' not in n and not s.get('.shared',{}).get('size',0),f'{core}: disabled shared object remains')
        def symbol(name):
            require(name in n,f'{core}: missing {name}')
            return n[name]['address']
        for name in ['.init_array','.preinit_array','.ctors']:
            require(not s.get(name,{}).get('size',0),f'{core}: constructor array')
        for name in ['.init','.data','.bss','.stack']:require(name in s,f'{core}: missing section')
        init=s['.init'];data=s['.data'];bss=s['.bss'];stack=s['.stack']
        require(image['entry']==symbol('_start') and base.inside([image['entry'],1],[init['address'],init['size']]),f'{core}: entry mismatch')
        for name in ['.data','.bss','.stack']:
            require(base.inside([s[name]['address'],s[name]['size']],regions['RAM']),f'{core}: private section outside RAM')
        require([symbol('_data_vma'),symbol('_edata')]==[data['address'],data['address']+data['size']],f'{core}: data bounds')
        require([symbol('_sbss'),symbol('_ebss')]==[bss['address'],bss['address']+bss['size']],f'{core}: bss bounds')
        require([symbol('_susrstack'),symbol('_eusrstack')]==[stack['address'],sum(regions['RAM'])] and
                stack['size']==config['stack_bytes'] and stack['address']+stack['size']==sum(regions['RAM']),f'{core}: stack bounds')
        require(symbol('_ebss')<=stack['address'],f'{core}: static/stack collision')
        loads=[]
        for segment in image['segments']:
            if not segment['file_bytes']:continue
            require(base.inside([segment['paddr'],segment['file_bytes']],regions['FLASH']),f'{core}: load outside Flash')
            if base.inside([data['address'],data['size']],[segment['vaddr'],segment['file_bytes']]):
                loads.append(segment['paddr']+data['address']-segment['vaddr'])
        require(loads==[symbol('_data_lma')],f'{core}: data load bounds')
        for name,section in s.items():
            if not section['size'] or not section['flags']&2:continue
            area=[section['address'],section['size']]
            require(any(base.inside(area,r) for r in regions.values()),f'{core}: unmapped allocated section')
            occupied.append(area)
        rows.append(dict(core=core,shared_bytes=0,reserved_bytes=0,normal_ram_bytes=regions['RAM'][1],
                         static_to_stack_gap=stack['address']-(bss['address']+bss['size'])))
    for i,a in enumerate(occupied):
        require(all(not base.overlap(a,b) for b in occupied[i+1:]),'allocated sections overlap')
    return rows
