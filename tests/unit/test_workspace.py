"""The new workspace cannot collect legacy or physical-board scenarios."""
import configparser
import ast
import pathlib
import tomllib

REPO = pathlib.Path(__file__).resolve().parents[2]
TESTS = REPO / "tests"
HARDWARE = ("single", "loopback", "peer", "instrumented", "manual")


def test_default_collection_is_board_free():
    config = tomllib.loads((TESTS / "pyproject.toml").read_text())
    pytest_config = config["tool"]["pytest"]["ini_options"]
    assert pytest_config["testpaths"] == ["unit", "build"]
    assert set(HARDWARE) <= set(pytest_config["norecursedirs"])
    root_config = configparser.ConfigParser()
    root_config.read(REPO / "pytest.ini")
    assert root_config["pytest"]["testpaths"].split() == [
        "tests/unit", "tests/build"
    ]


def test_hardware_directories_are_non_executable_scaffolds():
    for directory in HARDWARE:
        paths = list((TESTS / directory).rglob("*"))
        assert (TESTS / directory / "README.ja.md").is_file()
        assert not [p for p in paths if p.is_file() and p.suffix != ".md"]


def test_new_workspace_has_no_board_sketch_or_legacy_harness():
    ignored = {".venv", ".pytest_cache", "__pycache__"}
    sources = [
        p for p in TESTS.rglob("*")
        if p.is_file() and not ignored.intersection(p.relative_to(TESTS).parts)
    ]
    assert not [p for p in sources if p.suffix == ".ino"]
    assert not [p for p in sources if p.name == "conftest.py"]
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
