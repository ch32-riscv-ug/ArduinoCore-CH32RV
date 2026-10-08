"""Maintenance tools must work without an archived test suite."""
import importlib.util
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]


def load_tool(relative, name):
    spec = importlib.util.spec_from_file_location(name, REPO / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_example_profiles_are_generated_without_a_test_harness():
    result = subprocess.run(
        [sys.executable, "tools/generate/sync_example_profiles.py", "--check"],
        cwd=REPO, capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_version_update_only_changes_bundled_example_pins(tmp_path, monkeypatch):
    tool = load_tool("tools/index/bump_version.py", "version_update")
    monkeypatch.setattr(tool, "ROOT", tmp_path)
    example = tmp_path / "libraries" / "Example" / "examples" / "Hello" / "sketch.yaml"
    archive = tmp_path / "archive" / "sketch.yaml"
    old = "    - platform: ch32-riscv-ug:ch32rv (1.0.0)\n"
    for path in (example, archive):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(old)
    assert tool.update_sketch_pins("1.0.1") == (1, 1)
    assert "(1.0.1)" in example.read_text()
    assert archive.read_text() == old


def test_package_acceptance_source_is_self_contained(tmp_path):
    tool = load_tool("tools/index/install_check.py", "package_install")
    for name, source in (("Blink", tool.BLINK), ("Acceptance", tool.ACCEPTANCE),
                         ("Libraries", tool.LIBRARIES)):
        directory = tool.sketch(tmp_path, name, source)
        assert (directory / f"{name}.ino").read_text() == source
        assert not (directory / "testcmd.h").exists()
        assert sorted(p.name for p in directory.iterdir()) == [f"{name}.ino"]


def test_ci_and_release_do_not_run_archived_tests():
    for name in ("ci.yml", "release.yml"):
        source = (REPO / ".github" / "workflows" / name).read_text()
        assert "tests-legacy" not in source
