"""Reject modified source bundles before producing publication results."""
import hashlib
import importlib.util
import json
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    'h417_audit', Path(__file__).parents[2] / 'platform/core_redesign/audit_h417_sources.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


@pytest.mark.parametrize('fault', ['none', 'content', 'size', 'blob', 'commit', 'tree', 'truncated', 'member'])
def test_bundle_integrity(tmp_path, fault):
    data = b'fixed source\n'
    (tmp_path / 'sdk').mkdir()
    (tmp_path / 'sdk/source.c').write_bytes(data)
    entry = {'path': 'source.c', 'bytes': len(data),
             'sha256': hashlib.sha256(data).hexdigest(),
             'git_blob_sha1': hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()}
    config = {'documents': [], 'sdk_files': [entry], 'sdk_commit': 'commit1', 'sdk_root_tree': 'tree1'}
    commit = {'sha': 'commit1', 'commit': {'tree': {'sha': 'tree1'}}}
    tree = {'sha': 'tree1', 'truncated': False,
            'tree': [{'type': 'blob', 'path': 'source.c', 'sha': entry['git_blob_sha1']}]}
    if fault == 'content':
        (tmp_path / 'sdk/source.c').write_bytes(b'modified src\n')
    elif fault == 'size':
        entry['bytes'] += 1
    elif fault == 'blob':
        entry['git_blob_sha1'] = 'wrong'
    elif fault == 'commit':
        commit['sha'] = 'other'
    elif fault == 'tree':
        commit['commit']['tree']['sha'] = 'other'
    elif fault == 'truncated':
        tree['truncated'] = True
    elif fault == 'member':
        tree['tree'] = []
    (tmp_path / 'sdk-commit-response.json').write_text(json.dumps(commit))
    (tmp_path / 'sdk-tree.json').write_text(json.dumps(tree))
    if fault == 'none':
        assert len(audit.audit(tmp_path, config)) == 1
    else:
        with pytest.raises(ValueError):
            audit.audit(tmp_path, config)
