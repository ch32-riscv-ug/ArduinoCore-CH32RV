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


def test_board_specific_example_builds_for_that_board_only(tmp_path):
    tools = REPO / "tools/generate"
    sys.path.insert(0, str(tools))
    try:
        requirements = load_tool("tools/generate/sketch_requirements.py", "sketch_requirements")
    finally:
        sys.path.remove(str(tools))
    sketch = tmp_path / "Template"
    sketch.mkdir()
    ino = sketch / "Template.ino"
    ino.write_text("/* A template.\n *\n * board: CH32X035:pnum=CH32X035F8U6\n */\n")
    assert requirements.target_board(sketch) == "CH32X035:pnum=CH32X035F8U6"
    ino.write_text("/* keyboard: any key */\n")
    assert requirements.target_board(sketch) is None
    for wrong in ("NOSUCHBOARD", "CH32X035:pnum=NOSUCHPART"):
        ino.write_text(f"/* board: {wrong} */\n")
        try:
            requirements.target_board(sketch)
        except requirements.BadRequirement:
            continue
        raise AssertionError(f"{wrong} was accepted")
