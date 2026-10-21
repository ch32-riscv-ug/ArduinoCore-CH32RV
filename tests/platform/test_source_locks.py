"""Offline integrity only: this does not verify upstream provenance or runtime."""
import hashlib
import importlib.util
import pathlib
import tomllib

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]

# A lock whose vendoring tool carries deliberate patches: the file on disk must
# hold each patch, and with it taken out must hash to the recorded upstream file.
# (None now: TinyUSB, the one that had them, is gone.)
PATCH_TOOLS = {}


def patch_tool(lock_name):
    path = PATCH_TOOLS.get(lock_name)
    if path is None:
        return None
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("lock_name", [
    "arduino-core-api.lock.toml",
    "lwip.lock.toml",
])
def test_source_inventory_and_hashes_match_lock(lock_name):
    lock = tomllib.loads((REPO / "vendor" / lock_name).read_text())
    tool = patch_tool(lock_name)
    patched = {p["path"] for p in tool.PATCHES} if tool else set()
    for source in lock["source"]:
        destination = REPO / source["dest"]
        entries = source["files"]
        expected = {entry["path"]: entry["sha256"] for entry in entries}
        assert expected, lock_name
        assert len(expected) == len(entries), "duplicate paths in lock"
        # A lock with `dirs` owns only those directories under dest; the rest is our code.
        roots = [destination / d for d in source.get("dirs", [])] or [destination]
        actual = {
            p.relative_to(destination).as_posix()
            for root in roots for p in root.rglob("*") if p.is_file()
        }
        additions = set(source.get("ours", []))
        assert actual - additions == set(expected), lock_name
        for relative, digest in expected.items():
            path = destination / relative
            assert path.resolve().is_relative_to(destination.resolve())
            data = path.read_bytes()
            if relative in patched:
                data = tool.unpatched(relative, data)
                assert data is not None, f"patch missing: {path}"
            assert hashlib.sha256(data).hexdigest() == digest, str(path)
