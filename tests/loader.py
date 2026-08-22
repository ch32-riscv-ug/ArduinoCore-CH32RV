"""Import a harness that lives outside any package.

Not in conftest.py, and that is the whole point. pytest imports every
conftest.py under the plain module name `conftest`, so `from conftest import
load` reaches whichever one was loaded last - and one command that names both a
category and a manual test

    uv run pytest sketches manual/gpio_loopback/gpio_loopback.py

made that manual/gpio_loopback/conftest.py, with an ImportError that names
neither file as the cause. A module with its own name cannot be shadowed.

tests/ is on sys.path because conftest.py sits in it, so `from loader import
load` works from any category directory without anything being wired up.
"""
import importlib.util
import pathlib

REPO = pathlib.Path(__file__).resolve().parents[1]


def load(relative_path, name):
    """Import a harness by path, e.g. load("tests/compile/compile_matrix.py").

    The harnesses are plain scripts rather than an installed package: each one
    also runs under a bare `uv run`, which is what makes a failure reproducible
    outside pytest.
    """
    path = REPO / relative_path
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
