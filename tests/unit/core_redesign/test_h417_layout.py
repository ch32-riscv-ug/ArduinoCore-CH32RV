"""Placement verifier counterexamples; board-free and no compiler required."""
import copy
import importlib.util
import json
from pathlib import Path
import struct

import pytest

REPO=Path(__file__).resolve().parents[3]
CONFIG=json.loads((REPO/'planning/core-redesign/h417-layout-inputs.json').read_text())
spec=importlib.util.spec_from_file_location('h417_layout_check',REPO/'tests/platform/core_redesign/h417_link_layout.py')
layout=importlib.util.module_from_spec(spec);spec.loader.exec_module(layout)


def fixture():
    images={}
    for core,settings in CONFIG['cores'].items():
        regions=copy.deepcopy(settings['regions']);flash=regions['FLASH'][0];ram,length=regions['RAM']
        shared=CONFIG['shared_origin'];stack=ram+length-2048
        def s(address,size,type=1,flags=3,alignment=4):
            return dict(address=address,size=size,type=type,flags=flags,alignment=alignment)
        sections={'.init':s(flash,16,flags=6),'.data':s(ram,8),'.bss':s(ram+8,52,type=8),
                  '.stack':s(stack,2048,type=8),'.shared':s(shared,256,type=8,alignment=16)}
        symbols={name:dict(address=address,size=0) for name,address in
                 [('_start',flash),('_data_vma',ram),('_edata',ram+8),('_data_lma',flash+16),
                  ('_sbss',ram+8),('_ebss',ram+60),('_susrstack',stack),('_eusrstack',stack+2048)]}
        symbols['experiment_shared']=dict(address=shared,size=256)
        images[core]=dict(entry=flash,flags=3,sections=sections,symbols=symbols,regions=regions,
                         segments=[dict(vaddr=flash,paddr=flash,file_bytes=16),dict(vaddr=ram,paddr=flash+16,file_bytes=8)])
    return images


def test_valid_pair_and_touching_region_boundaries():
    rows=layout.validate_pair(fixture(),CONFIG,256)
    assert [r['shared_bytes'] for r in rows]==[256,256]
    assert all(r['static_to_stack_gap']>0 for r in rows)
    assert not layout.overlap([1,3],[4,2])
    assert layout.inside([4,0],[1,3])


FAULTS=['shared-address','shared-size','shared-progbits','shared-executable','shared-align8','shared-align24',
        'missing-symbol','symbol-size','region','abi','abi-difference','ctor','bss-bounds','data-load',
        'stack-bounds','stack-size','bss-stack','outside-private','load-shared','orphan-shared',
        'orphan-overlap','entry','core','reserved-overlap','both-rve','both-no-rvc']


@pytest.mark.parametrize('fault',FAULTS)
def test_invalid_layout_rejected(fault):
    images=fixture();config=copy.deepcopy(CONFIG);v=images['V5F'];s=v['sections'];n=v['symbols']
    if fault=='shared-address':s['.shared']['address']+=16
    elif fault=='shared-size':s['.shared']['size']+=16
    elif fault=='shared-progbits':s['.shared']['type']=1
    elif fault=='shared-executable':s['.shared']['flags']=7
    elif fault=='shared-align8':s['.shared']['alignment']=8
    elif fault=='shared-align24':s['.shared']['alignment']=24
    elif fault=='missing-symbol':del n['experiment_shared']
    elif fault=='symbol-size':n['experiment_shared']['size']-=1
    elif fault=='region':v['regions']['RAM_SHARED'][0]+=16
    elif fault=='abi':v['flags']=1
    elif fault=='abi-difference':v['flags']|=8
    elif fault=='ctor':s['.init_array']=dict(size=4)
    elif fault=='bss-bounds':n['_ebss']['address']+=4
    elif fault=='data-load':n['_data_lma']['address']+=4
    elif fault=='stack-bounds':n['_susrstack']['address']+=4
    elif fault=='stack-size':
        s['.stack']['size']-=4;n['_eusrstack']['address']-=4
    elif fault=='bss-stack':
        end=s['.stack']['address']+4
        s['.bss']['size']=end-s['.bss']['address'];n['_ebss']['address']=end
    elif fault=='outside-private':s['.bss']['address']=CONFIG['shared_origin']
    elif fault=='load-shared':v['segments'][0]['paddr']=CONFIG['shared_origin']
    elif fault=='orphan-shared':s['.orphan']=dict(address=CONFIG['shared_origin']+512,size=16,flags=3)
    elif fault=='orphan-overlap':s['.orphan']=dict(address=s['.data']['address'],size=4,flags=3)
    elif fault=='entry':v['entry']+=4
    elif fault=='core':del images['V3F']
    elif fault=='reserved-overlap':
        area=copy.deepcopy(CONFIG['cores']['V3F']['regions']['RAM'])
        config['cores']['V5F']['regions']['EXTRA']=area
        v['regions']['EXTRA']=area
    elif fault=='both-rve':
        for image in images.values():image['flags']=11
    elif fault=='both-no-rvc':
        for image in images.values():image['flags']=2
    with pytest.raises(ValueError):layout.validate_pair(images,config,256)


def raw_elf():
    names=b'\0.shared\0.shstrtab\0';offset=84;table=104
    raw=bytearray(table+3*40)
    ident=b'\x7fELF\x01\x01\x01'+bytes(9)
    struct.pack_into('<16sHHIIIIIHHHHHH',raw,0,ident,2,243,1,0,52,table,3,52,32,1,40,3,2)
    struct.pack_into('<8I',raw,52,1,offset,CONFIG['shared_origin'],CONFIG['shared_origin'],0,256,6,16)
    raw[offset:offset+len(names)]=names
    struct.pack_into('<10I',raw,table+40,1,8,3,CONFIG['shared_origin'],offset,256,0,0,16,0)
    struct.pack_into('<10I',raw,table+80,9,3,0,0,offset,len(names),0,0,1,0)
    return raw


def test_elf_decoder_reads_nobits_without_treating_it_as_file_data():
    raw=raw_elf();image=layout.decode(raw,'20178000 00000100 B experiment_shared\n','RAM_SHARED 0x20178000 0x8000 xrw\n')
    assert image['sections']['.shared']['type']==8
    assert image['sections']['.shared']['size']==256
    assert image['segments'][0]['file_bytes']==0
    assert image['symbols']['experiment_shared']['size']==256
    assert image['regions']['RAM_SHARED']==[CONFIG['shared_origin'],32768]


@pytest.mark.parametrize('fault',['short','class','machine','tables','name-table','file-range','alignment','segment-size'])
def test_malformed_elf_rejected(fault):
    raw=raw_elf()
    if fault=='short':raw=raw[:20]
    elif fault=='class':raw[4]=2
    elif fault=='machine':struct.pack_into('<H',raw,18,40)
    elif fault=='tables':struct.pack_into('<I',raw,32,len(raw))
    elif fault=='name-table':struct.pack_into('<I',raw,104+80+16,len(raw))
    elif fault=='file-range':struct.pack_into('<I',raw,104+80+20,len(raw))
    elif fault=='alignment':struct.pack_into('<I',raw,104+40+32,24)
    elif fault=='segment-size':struct.pack_into('<I',raw,52+16,257)
    with pytest.raises(ValueError):layout.decode(raw,'','')
