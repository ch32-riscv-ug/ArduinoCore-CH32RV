"""The bundled examples compile.

Examples are the only code in the repository that no other test exercises, and
they are what a new user runs first. Building them on the widest and the
narrowest board is the cheapest way to keep them honest.
"""
import pytest

from loader import load

pytestmark = pytest.mark.slow

harness = load("tests/compile/compile_examples.py", "compile_examples")


@pytest.fixture(scope="module")
def built(repo, gcc_bin, arduino_cli, workdir):
    return harness.run(workdir / "examples")


def test_every_example_compiles(built):
    """A Failure would already have aborted; this pins the count."""
    assert len(built["examples"]) >= 10, built["examples"]


def test_each_library_has_at_least_one_example(repo, built):
    """A bundled library with no example is a library nobody can start with."""
    libraries = {p.parent.name for p in (repo / "libraries").glob("*/library.properties")}
    with_examples = {library for library, _name, _src in built["examples"]}
    # TinyUSB is the stack, not a user-facing API yet; it gets examples when
    # the CDC glue lands (docs/todo.ja.md).
    missing = libraries - with_examples - {"TinyUSB"}
    assert not missing, f"no examples for {sorted(missing)}"


def test_every_library_documents_itself(repo):
    """A bundled library ships README.md, README.ja.md and keywords.txt.

    ADR-0013 makes documentation part of what "bundled" means: the examples
    show how, and the README says what to watch out for. Checking it here is
    what keeps a new library from arriving without either.
    """
    missing = []
    for properties in sorted((repo / "libraries").glob("*/library.properties")):
        library = properties.parent
        for name in ("README.md", "README.ja.md", "keywords.txt"):
            if not (library / name).exists():
                missing.append(f"{library.name}/{name}")
    assert not missing, f"missing: {missing}"
