#!/usr/bin/env python3
"""Inventory recorded measurement manifests and verify their local archives.

This checks artifact preservation, not the meaning or completeness of experiments.
Only archives explicitly named in measurement JSON are checked; no discovery by
similar filenames and no comparison of historical inputs against current sources.
"""

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def references(value, pointer=""):
    if isinstance(value, dict):
        for key, item in value.items():
            location = f"{pointer}/{key}"
            if isinstance(item, str) and item.startswith("work/") and item.endswith((".tar.gz", ".tgz")):
                prefix = "archive_" if key == "archive" else ""
                yield location, item, value.get(prefix + "sha256"), value.get(prefix + "bytes")
            elif isinstance(item, (dict, list)):
                yield from references(item, location)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from references(item, f"{pointer}/{index}")


def inventory(root):
    records, failures, cache = [], [], {}
    for path in sorted((root / "planning/core-redesign/tasks").rglob("*measurements.json")):
        relative = path.relative_to(root).as_posix()
        document = json.loads(path.read_text())
        archives = []
        for pointer, name, expected_hash, expected_bytes in references(document):
            target = (root / name).resolve()
            if not target.is_relative_to(root.resolve()):
                raise ValueError(f"Archive escapes repository: {name}")
            if name not in cache:
                cache[name] = ({"present": True, "sha256": sha256(target), "bytes": target.stat().st_size}
                               if target.is_file() else {"present": False})
            actual = cache[name]
            matched = (actual["present"] and expected_hash is not None and expected_bytes is not None
                       and actual["sha256"] == expected_hash and actual["bytes"] == expected_bytes)
            row = dict(pointer=pointer, path=name, expected_sha256=expected_hash,
                       expected_bytes=expected_bytes, observed=actual,
                       status="matched" if matched else "missing-or-unverified")
            archives.append(row)
            if not matched:
                failures.append(dict(manifest=relative, **row))
        records.append(dict(path=relative, sha256=sha256(path), archives=archives,
                            archive_reference_status="recorded" if archives else "not-recorded-in-manifest"))
    return dict(schema=1, scope="Artifact inventory only; not semantic verification or phase completion",
                manifests=records, manifest_count=len(records), unique_archive_count=len(cache),
                archive_reference_count=sum(len(row["archives"]) for row in records),
                manifests_without_archive_reference=[r["path"] for r in records if not r["archives"]],
                failures=failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--out", type=Path, required=True, help="New report path; existing files are not overwritten")
    args = parser.parse_args()
    report = inventory(args.root.resolve())
    with args.out.open("x") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(json.dumps({key: report[key] for key in ["manifest_count", "unique_archive_count", "archive_reference_count", "failures"]}))
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
