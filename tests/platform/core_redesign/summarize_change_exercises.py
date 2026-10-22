#!/usr/bin/env python3
"""Normalize explicit before/after selectors and historical/new ABI pairs.
No tests are rerun, no machine timing becomes a human educational score.
"""
import argparse
import csv
import difflib
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT/'planning/core-redesign'
INPUT=BASE/'change-exercises-records.json'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def summarize(out):
    out.mkdir(parents=True,exist_ok=False);records=json.loads(INPUT.read_text());report=dict(scope=records['scope'],inputs={str(INPUT.relative_to(ROOT)):sha(INPUT)},records=[])
    for spec in records['records']:
        if spec['status']=='pending':report['records'].append(spec);continue
        folder=BASE/'tasks/EXP-07'/spec['directory'];before=folder/'before.cpp';after=folder/'after.cpp'
        patch=list(difflib.unified_diff(before.read_text().splitlines(keepends=True),after.read_text().splitlines(keepends=True),fromfile=str(before.relative_to(ROOT)),tofile=str(after.relative_to(ROOT))))
        (out/(spec['id']+'.patch')).write_text(''.join(patch))
        inputs=[before,after,BASE/spec['abi_csv']]
        if spec.get('historical_manifest'):inputs.append(BASE/spec['historical_manifest'])
        inputs += [ROOT/name for name in spec['shared_preparation']]
        for p in inputs:
            report['inputs'][str(p.relative_to(ROOT))]=sha(p)
            target=out/'inputs'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.read_bytes())
        rows=list(csv.DictReader((BASE/spec['abi_csv']).open()));groups={}
        if spec.get('historical_manifest'):
            manifest=json.loads((BASE/spec['historical_manifest']).read_text())
            if spec.get('matrix_sha_pointer'):
                expected=manifest
                for key in spec['matrix_sha_pointer']:expected=expected[key]
                assert sha(BASE/spec['abi_csv'])==expected,spec['id']
            else:
                source_rows=manifest
                for key in spec['manifest_rows_pointer']:source_rows=source_rows[key]
                for row in rows:
                    assert any(all(k in original and str(original[k])==v for k,v in row.items()) for original in source_rows),(spec['id'],row)

        for row in rows:
            if row["case"] in spec.get("exclude_cases",[]):continue
            if spec.get('candidate_pattern'):
                match=re.search(spec['candidate_pattern'],row['case'])
                if not match:continue
                row['candidate']=match[1]
            kind=None
            for side in ['before','after']:
                if all(row.get(k)==v for k,v in spec[side+'_filter'].items()) and (not spec.get(side+'_suffix') or row['case'].endswith(spec[side+'_suffix'])):
                    assert kind is None,(spec['id'],row);kind=side
            if not kind:continue
            key=tuple(row[k] for k in spec['group_axes']);pair=groups.setdefault(key,{})
            assert kind not in pair,(spec['id'],key,kind);pair[kind]=row
        pairs=[]
        for key,pair in sorted(groups.items()):
            assert set(pair)=={'before','after'},(spec['id'],key,pair)
            item=dict(axes=dict(zip(spec['group_axes'],key)),before_case=pair['before']['case'],after_case=pair['after']['case'])
            for label,field in [('image',spec['image_field']),('static_ram',spec['ram_field']),('application_object',spec['object_field'])]:
                item[label]=None if field is None else dict(before=int(pair['before'][field]),after=int(pair['after'][field]),delta=int(pair['after'][field])-int(pair['before'][field]))
            pairs.append(item)
        assert len(pairs)==spec['expected_pairs'],(spec['id'],len(pairs),spec['expected_pairs'])
        candidate_axis='candidate' if 'candidate' in spec['group_axes'] else 'family'
        assert len({item['axes'][candidate_axis] for item in pairs})==4,spec['id']
        report['records'].append({**spec,'logical_edited_files':1,'selection_lines_added':sum(line.startswith('+') and not line.startswith('+++') for line in patch),
            'selection_lines_removed':sum(line.startswith('-') and not line.startswith('---') for line in patch),'shared_preparation_metrics':[dict(path=name,bytes=(ROOT/name).stat().st_size,lines=len((ROOT/name).read_text().splitlines())) for name in spec['shared_preparation']],
            'abi_pair_count':len(pairs),'pairs':pairs,'human_review':None,'generation_time_as_score':False})
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    return report
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
    report=summarize(args.out)
    print(json.dumps([dict(id=r['id'],status=r['status'],pairs=r.get('abi_pair_count')) for r in report['records']]))
