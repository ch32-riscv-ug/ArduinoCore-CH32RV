"""Audit a caller-provided, pinned download bundle and enumerate visibility orders.

Offline. Does not install SDK files, compile firmware, or contact hardware.
"""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]


def audit(bundle, config):
    verified = []
    for entry in config['documents'] + config['sdk_files']:
        relative = Path(entry['file']) if 'file' in entry else Path('sdk') / entry['path']
        data = (bundle / relative).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != entry['bytes'] or digest != entry['sha256']:
            raise ValueError(f'content mismatch: {relative}')
        if 'git_blob_sha1' in entry:
            blob = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
            if blob != entry['git_blob_sha1']:
                raise ValueError(f'Git blob mismatch: {relative}')
        verified.append({'file': str(relative), 'bytes': len(data), 'sha256': digest})
    commit = json.loads((bundle / 'sdk-commit-response.json').read_text())
    tree = json.loads((bundle / 'sdk-tree.json').read_text())
    if (commit['sha'] != config['sdk_commit'] or
            commit['commit']['tree']['sha'] != config['sdk_root_tree'] or
            tree['sha'] != config['sdk_root_tree'] or tree['truncated']):
        raise ValueError('SDK commit/tree mismatch or truncated tree')
    blobs = {e['path']: e['sha'] for e in tree['tree'] if e['type'] == 'blob'}
    for entry in config['sdk_files']:
        if blobs.get(entry['path']) != entry['git_blob_sha1']:
            raise ValueError(f'tree member mismatch: {entry["path"]}')
    return verified


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    config_path = REPO / 'planning/core-redesign/h417-inputs.json'
    config = json.loads(config_path.read_text())
    verified = audit(args.bundle, config)
    model_path = REPO / 'tests/unit/core_redesign/h417_publication_model.py'
    spec = importlib.util.spec_from_file_location('h417_model', model_path)
    model = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(model)
    assert list(model.EVENTS) == config['model']['events']
    output = {'config_sha256': hashlib.sha256(config_path.read_bytes()).hexdigest(),
              'model_sha256': hashlib.sha256(model_path.read_bytes()).hexdigest(),
              'verified_sources': verified,
              'visibility': [model.summarize(*flags) for flags in
                             itertools.product((False, True), repeat=3)],
              'limits': 'Finite declared visibility constraints; not an ISA, cache, DMA, reset or timing proof.'}
    with args.out.open('x') as stream:
        json.dump(output, stream, indent=2)
        stream.write('\n')
    print(f'{len(verified)} source files verified; 8 visibility contracts enumerated')


if __name__ == '__main__':
    main()
