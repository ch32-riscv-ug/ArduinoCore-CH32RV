"""Default collection stays board-free even with opt-in hardware contracts installed."""
import ast
import pathlib
import tomllib

REPO = pathlib.Path(__file__).resolve().parents[2]
TESTS = REPO / "tests"
HARDWARE = ("single", "loopback", "peer", "instrumented", "manual")


def test_default_collection_is_board_free():
    config = tomllib.loads((TESTS / "pyproject.toml").read_text())
    pytest_config = config["tool"]["pytest"]["ini_options"]
    assert pytest_config["testpaths"] == ["unit", "platform"]
    assert set(HARDWARE) | {"hw"} <= set(pytest_config["norecursedirs"])
    # The one pytest configuration is tests/pyproject.toml; from the repository root it is named with -c.
    assert not (REPO / "pytest.ini").exists()


def test_hardware_contracts_cannot_trigger_an_upload_when_collected():
    for directory in HARDWARE:
        assert (TESTS / directory / "README.ja.md").is_file()
        assert not list((TESTS / directory).rglob("*.ino"))


def test_new_workspace_has_no_board_sketch_or_legacy_harness():
    ignored = {".venv", ".pytest_cache", "__pycache__"}
    sources = [
        p for p in TESTS.rglob("*")
        if p.is_file() and not ignored.intersection(p.relative_to(TESTS).parts)
    ]
    # Sketches live in sketch_support, or beside their test in hw/<test>/ (pytest-embedded-arduino-cli's layout,
    # outside the default collection); hw/conftest.py is the one conftest.
    for p in sources:
        if p.suffix == ".ino":
            parts = p.relative_to(TESTS).parts
            assert "sketch_support" in parts or parts[0] == "hw", str(p)
    assert [p.relative_to(TESTS) for p in sources if p.name == "conftest.py"] in (
        [], [pathlib.Path("hw/conftest.py")])
    config = tomllib.loads((TESTS / "pyproject.toml").read_text())
    assert config["project"]["dependencies"] == ["pytest>=8"]
    for path in sources:
        if path.suffix != ".py":
            continue
        tree = ast.parse(path.read_text())
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                modules = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom):
                modules = [node.module or ""]
            else:
                continue
            assert not set(modules) & {
                "loader", "sketch_requirements", "bench_kit", "tracekit"
            }, str(path)
