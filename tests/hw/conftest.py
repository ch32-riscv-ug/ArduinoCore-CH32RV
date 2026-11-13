"""The bench's hooks and fixtures (harness/pytest_bench.py): which profile can run which case, the workspace, the
build id and the ch32_target fixture. Nothing else lives here."""
from harness.pytest_bench import (  # noqa: F401
    ch32_target, pytest_arduino_cli_skip_reason, pytest_configure, pytest_runtest_setup,
)
