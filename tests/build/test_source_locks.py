"""Offline integrity only: this does not verify upstream provenance or runtime."""
import hashlib
import pathlib
import tomllib

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("lock_name", [
    "arduino-core-api.lock.toml",
    "tinyusb.lock.toml",
])
def test_source_inventory_and_hashes_match_lock(lock_name):
    lock = tomllib.loads((REPO / "vendor" / lock_name).read_text())
    for source in lock["source"]:
        destination = REPO / source["dest"]
        entries = source["files"]
        expected = {entry["path"]: entry["sha256"] for entry in entries}
        assert expected, lock_name
        assert len(expected) == len(entries), "duplicate paths in lock"
        actual = {
            p.relative_to(destination).as_posix()
            for p in destination.rglob("*") if p.is_file()
        }
        additions = set(source.get("ours", []))
        assert actual - additions == set(expected), lock_name
        for relative, digest in expected.items():
            path = destination / relative
            assert path.resolve().is_relative_to(destination.resolve())
            assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, str(path)
