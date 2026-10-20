"""Derived reservations, including disabled sharing and mixed-image guards."""
import copy
import importlib.util
import json
from pathlib import Path

import pytest

UNIT=Path(__file__).parent;REPO=UNIT.parents[2]
CONFIG=json.loads((REPO/'planning/core-redesign/h417-layout-inputs.json').read_text())
spec=importlib.util.spec_from_file_location('h417_reservation_check',REPO/'tests/platform/core_redesign/h417_reservation.py')
reservation=importlib.util.module_from_spec(spec);spec.loader.exec_module(reservation)
spec=importlib.util.spec_from_file_location('h417_layout_fixture',UNIT/'test_h417_layout.py')
baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline)


def images(capacity):
    result=baseline.fixture();selected=reservation.geometry(CONFIG,capacity)
    for core,image in result.items():
        regions=copy.deepcopy(selected['cores'][core]['regions']);image['regions']=regions
        stack=sum(regions['RAM'])-2048
        image['sections']['.stack']['address']=stack
        image['symbols']['_susrstack']['address']=stack
        image['symbols']['_eusrstack']['address']=stack+2048
        if capacity:
            image['sections']['.shared']['address']=selected['shared_origin']
            image['symbols']['experiment_shared']['address']=selected['shared_origin']
        else:
            del image['sections']['.shared'];del image['symbols']['experiment_shared']
        for name,value in [('capacity',capacity),('origin',selected['shared_origin']),('version',1)]:
            image['symbols']['__experiment_shared_'+name]=dict(address=value,size=0)
    return result


@pytest.mark.parametrize('capacity,origin',[(0,0x20180000),(256,0x2017ff00),(1024,0x2017fc00),(32768,0x20178000)])
def test_top_allocation_returns_only_v3_normal_ram(capacity,origin):
    selected=reservation.geometry(CONFIG,capacity)
    assert selected['shared_origin']==origin
    assert selected['cores']['V3F']['regions']['RAM'][1]==458496-capacity
    assert selected['cores']['V5F']['regions']['RAM'][1]==261376
    rows=reservation.validate(images(capacity),CONFIG,capacity,256 if capacity else 0)
    assert len(rows)==2


@pytest.mark.parametrize('capacity',[-16,1,255,32784,True,False,'256',256.0])
def test_invalid_capacity_is_not_rounded_silently(capacity):
    with pytest.raises(ValueError):reservation.geometry(CONFIG,capacity)


@pytest.mark.parametrize('capacity',[0,16,256,1024,32768])
@pytest.mark.parametrize('core',['V3F','V5F'])
def test_linker_derivation_changes_budget_and_explicit_object_input(core,capacity):
    source='RAM_SHARED (xrw) : ORIGIN = (0x20178000), LENGTH = 32K\n'
    if core=='V3F':source+='RAM (xrw) : LENGTH = (448K-256-32K)\n'
    else:source+='INPUT(\n ../../V3F/obj/User/shared.o\n)\n'
    output=reservation.derive(source,core,CONFIG,capacity)
    assert f'LENGTH = {capacity}' in output
    assert f'__experiment_shared_capacity = {capacity};' in output
    assert f'ASSERT(SIZEOF(.shared) <= {capacity},' in output
    if core=='V5F':assert ('INPUT(' in output)==bool(capacity)
    else:assert f'LENGTH = (448K-256-{capacity})' in output


@pytest.mark.parametrize('source,core',[
    ('','V3F'),('ORIGIN = (0x20178000), LENGTH = 32K\n','V3F'),
    ('ORIGIN = (0x20178000), LENGTH = 32K\n','V5F'),
    ('ORIGIN = (0x20178000), LENGTH = 32K\n','unknown'),
])
def test_unrecognized_source_shape_rejected(source,core):
    with pytest.raises(ValueError):reservation.derive(source,core,CONFIG,0)


@pytest.mark.parametrize('fault',['capacity','origin','version','missing'])
@pytest.mark.parametrize('capacity',[0,256])
def test_matching_geometry_requires_configuration_symbols(capacity,fault):
    pair=images(capacity)
    if fault=='missing':del pair['V5F']['symbols']['__experiment_shared_capacity']
    else:pair['V5F']['symbols']['__experiment_shared_'+fault]['address']+=1
    with pytest.raises(ValueError,match='configuration mismatch'):
        reservation.validate(pair,CONFIG,capacity,256 if capacity else 0)


OFF_FAULTS=['payload','object-symbol','section','region','abi','ctor','missing-section','entry',
            'data-bounds','data-load','bss-bounds','stack-bounds','stack-size','static-stack',
            'private-outside','load-outside','orphan-outside','orphan-overlap','missing-core']


@pytest.mark.parametrize('fault',OFF_FAULTS)
def test_disabled_mode_still_checks_private_initialization_and_stack(fault):
    pair=images(0);v=pair['V3F'];s=v['sections'];n=v['symbols'];payload=0
    if fault=='payload':payload=16
    elif fault=='object-symbol':n['experiment_shared']=dict(address=0x20180000,size=0)
    elif fault=='section':s['.shared']=dict(address=0x20180000,size=16,type=8,flags=3)
    elif fault=='region':v['regions']['RAM'][1]-=16
    elif fault=='abi':v['flags']=11
    elif fault=='ctor':s['.init_array']=dict(size=4)
    elif fault=='missing-section':del s['.bss']
    elif fault=='entry':v['entry']+=4
    elif fault=='data-bounds':n['_edata']['address']+=4
    elif fault=='data-load':n['_data_lma']['address']+=4
    elif fault=='bss-bounds':n['_ebss']['address']+=4
    elif fault=='stack-bounds':n['_susrstack']['address']+=4
    elif fault=='stack-size':s['.stack']['size']-=4
    elif fault=='static-stack':
        end=s['.stack']['address']+4;s['.bss']['size']=end-s['.bss']['address'];n['_ebss']['address']=end
    elif fault=='private-outside':s['.data']['address']=0x20000000
    elif fault=='load-outside':v['segments'][0]['paddr']=0x20000000
    elif fault=='orphan-outside':s['.orphan']=dict(address=0x20000000,size=16,flags=3)
    elif fault=='orphan-overlap':s['.orphan']=dict(address=s['.data']['address'],size=4,flags=3)
    elif fault=='missing-core':del pair['V5F']
    with pytest.raises(ValueError):reservation.validate(pair,CONFIG,0,payload)
