"""Import a harness that lives outside any package.

Not in conftest.py, and that is the whole point. pytest imports every
conftest.py under the plain module name `conftest`, so `from conftest import
load` reaches whichever one was loaded last - and one command that names both a
category and a manual test

    uv run pytest build/compile manual/gpio_loopback/gpio_loopback.py

made that manual/gpio_loopback/conftest.py, with an ImportError that names
neither file as the cause. A module with its own name cannot be shadowed.

tests-legacy/ is on sys.path because conftest.py sits in it, so `from loader import
load` works from any category directory without anything being wired up.
"""
import importlib.util
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]


def load(relative_path, name):
    """Import a harness by path, e.g. load("tests-legacy/build/compile/compile_matrix.py").

    The harnesses are plain scripts rather than an installed package: each one
    also runs under a bare `uv run`, which is what makes a failure reproducible
    outside pytest.
    """
    path = REPO / relative_path
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    # Registered before it runs, as importlib does: a @dataclass under
    # `from __future__ import annotations` looks its module up in sys.modules.
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


# ---------------------------------------------------------------- where the tools are
# tools/index/fetch_tools.py puts what the tests need under <repo>/.tools, at the
# versions tools/index/tools_*.json pins. An environment variable overrides each,
# for a bench that keeps its own copy somewhere else.

def find_gcc_bin() -> str | None:
    """The toolchain's bin directory: CH32_GCC_BIN, else <repo>/.tools."""
    import os
    override = os.environ.get("CH32_GCC_BIN")
    if override:
        return override
    root = REPO / ".tools" / "xpack-riscv-none-elf-gcc"
    found = sorted(d for d in root.glob("*") if (d / "bin").is_dir())
    return str(found[-1] / "bin") if found else None


def find_tables() -> str | None:
    """The ch32-device-data checkout root: CH32_TABLES, else <repo>/.tools.

    The root, not a tables/ subdirectory: upstream split the flat layout into
    catalog/ evidence/ index/, and consumers address tables through that split
    (tools/generate/generate.py table_relpath).
    """
    import os
    override = os.environ.get("CH32_TABLES")
    if override:
        return override
    d = REPO / ".tools" / "ch32-device-data"
    return str(d) if d.is_dir() else None
