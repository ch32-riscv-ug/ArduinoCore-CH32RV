#!/usr/bin/env python3
"""Offline V006 source provenance, finite wake interleavings and AWU intervals.

No hardware is accessed; source downloads are supplied by the caller.
"""
import argparse
import csv
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import traceback
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
UNIT = REPO/'tests/unit/core_redesign'
INPUT = REPO/'planning/core-redesign/sleep-inputs.json'
spec = importlib.util.spec_from_file_location('sleep_boundary', UNIT/'sleep_boundary_model.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def portable(value):
    if isinstance(value, Fraction):
        return dict(numerator=value.numerator, denominator=value.denominator, seconds=float(value))
    if isinstance(value, dict):
        return {k: portable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [portable(v) for v in value]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--research', type=Path, required=True)
    parser.add_argument('--junit', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    config = json.loads(INPUT.read_text())
    record = dict(status='running', scope=__doc__, inputs={}, sources={}, boundary_summary=[], intervals=[])
    try:
        for path in [Path(__file__).resolve(), INPUT, UNIT/'sleep_boundary_model.py', UNIT/'test_sleep_boundary.py']:
            record['inputs'][str(path.relative_to(REPO))] = sha(path)
            dest = args.out/'inputs'/path.relative_to(REPO)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
        for name in ['reference', 'datasheet', 'core']:
            source = config['sources'][name]
            path = args.research/f'{name}-en.pdf'
            assert path.read_bytes().startswith(b'%PDF-')
            assert sha(path) == source['sha256'], name
            record['sources'][name] = source
        commit_data = json.loads((args.research/'sdk-commit.json').read_text())
        commit = commit_data['sha']
        assert commit == config['sources']['sdk']['commit'], 'SDK commit mismatch'
        tree = json.loads((args.research/'sdk-tree.json').read_text())
        assert tree['sha'] == commit_data['commit']['tree']['sha'], 'SDK root tree mismatch'
        assert not tree['truncated'], 'SDK tree was truncated'
        blobs = {row['path']: row['sha'] for row in tree['tree'] if row['type'] == 'blob'}
        sdk = []
        for row in json.loads((args.research/'sdk-files.json').read_text()):
            path = args.research/'sdk'/row['path']
            raw = path.read_bytes()
            assert sha(path) == row['sha256'], row['path']
            assert hashlib.sha1(f'blob {len(raw)}\0'.encode()+raw).hexdigest() == blobs[row['path']], row['path']
            sdk.append(row)
        assert len(sdk) == 6
        record['sources']['sdk'] = dict(**config['sources']['sdk'], files=sdk)
        suites = list(ET.parse(args.junit).getroot().iter('testsuite'))
        assert suites and all(int(s.get('failures', 0)) == int(s.get('errors', 0)) == int(s.get('skipped', 0)) == 0 for s in suites)
        record['tests'] = sum(int(s.get('tests', 0)) for s in suites)
        record['junit_sha256'] = sha(args.junit)
        shutil.copy2(args.junit, args.out/'junit.xml')
        scenarios = []
        for backend in config['protocol_backends']:
            for protocol in model.PROTOCOLS:
                rows = model.enumerate_protocol(protocol, backend, config['max_posts'])
                failures = [row for row in rows if row['stranded']]
                record['boundary_summary'].append(dict(backend=backend['id'], protocol=protocol,
                    scenarios=len(rows), stranded=len(failures), counterexample=failures[0] if failures else None))
                scenarios.append(dict(backend=backend, protocol=protocol, cases=rows))
        (args.out/'boundary-cases.json').write_text(json.dumps(scenarios, indent=2)+'\n')
        for window in config['maximum_windows_for_sensitivity']:
            candidates = list(model.awu_intervals(config['prescalers'], window, config['lsi_hz_min_nominal_max']))
            with (args.out/f'awu-window{window}.csv').open('w', newline='') as f:
                writer = csv.writer(f, lineterminator='\n')
                writer.writerow(['code', 'divisor', 'window', 'ticks', 'minimum_s', 'nominal_s', 'maximum_s'])
                writer.writerows([r['code'], r['divisor'], r['window'], r['ticks'], str(r['minimum']), str(r['nominal']), str(r['maximum'])] for r in candidates)
            for target in config['target_seconds']:
                for policy in config['policies']:
                    chosen = model.choose_counter_interval(candidates, Fraction(target), policy)
                    record['intervals'].append(dict(maximum_window=window, target_seconds=target,
                        policy=policy, choice=portable(chosen)))
        record['status'] = 'passed'
    except Exception as error:
        record.update(status='failed', error=repr(error), traceback=traceback.format_exc())
    finally:
        (args.out/'result.json').write_text(json.dumps(record, indent=2)+'\n')
        manifest = {str(p.relative_to(args.out)): sha(p) for p in sorted(args.out.rglob('*')) if p.is_file() and p.name != 'sha256.json'}
        (args.out/'sha256.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(dict(status=record['status'], scenarios=sum(r['scenarios'] for r in record['boundary_summary']),
                         interval_comparisons=len(record['intervals']), error=record.get('error'))))
    return int(record['status'] != 'passed')


if __name__ == '__main__':
    sys.exit(main())
