"""Bench fixtures for the manual tests.

manual/ is excluded from collection (norecursedirs in pyproject.toml), so
nothing here runs unless a file is named on the command line. That is
deliberate: these tests flash the attached board, which is not something a bare
`pytest` should decide to do.

    cd tests
    uv run --env-file .env pytest manual/chip_info/chip_info.py -v -s
    uv run --env-file .env pytest manual/smoke/smoke.py -v -s

Which probe, which board and which port are not fixed on this bench, so they
come from the environment the same way the loopback pins do - see
tests/.env.example. Nothing here takes a pytest option, because pytest-embedded
already owns --port and --target and a second meaning for either would be worse
than an environment variable.

A bench that cannot run a test skips it rather than failing: an unplugged board
is not a regression. What *is* a failure is a board that is plugged in and does
not behave, which is the whole point of chip_info.py's assertions.
"""
import os
import pathlib
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
# One directory per manual test, the same shape gpio_loopback already had,
# so each keeps its sketch and its generated headers to itself.
for _tool in ("chip_info", "smoke", "uart_scan"):
    sys.path.insert(0, str(HERE / _tool))

import chip_info                                          # noqa: E402
import smoke                                              # noqa: E402
import uart_scan                                          # noqa: E402


@pytest.fixture(scope="session")
def probe_rs_dir() -> str:
    found = smoke.find_probe_rs()
    if not found:
        pytest.skip("probe-rs not found; run: uv run tools/index/fetch_tools.py")
    return found


@pytest.fixture(scope="session")
def attached(probe_rs_dir) -> list:
    """Every WCH-Link on the bench, asked once for the whole session.

    Asking costs a probe-rs invocation per probe, and every hardware test wants
    the same answer, so it is read once. CH32_PROBE narrows it to one probe when
    the bench has several.
    """
    records = chip_info.inventory(os.environ.get("CH32_PROBE"), probe_rs_dir)
    if not records:
        pytest.skip("no WCH-Link attached")
    return records


@pytest.fixture(scope="module")
def bench(attached):
    """The one board this module talks to, resolved once.

    Depends on `attached` so an empty bench skips rather than fails, then lets
    smoke.resolve_bench do the deciding - it is the same code the CLI uses, so
    the two cannot drift on which board a given chip means.
    """
    try:
        return smoke.resolve_bench(
            board=os.environ.get("CH32_BOARD"),
            pnum=os.environ.get("CH32_PNUM", "ANY"),
            port=os.environ.get("CH32_PORT"),
            probe=os.environ.get("CH32_PROBE"),
            serial=int(os.environ["CH32_SERIAL_INDEX"])
            if os.environ.get("CH32_SERIAL_INDEX") else None,
            force=bool(os.environ.get("CH32_FORCE")),
            baud=int(os.environ.get("CH32_BAUD", 115200)),
            seconds=float(os.environ.get("CH32_SECONDS", 4.0)),
            # Space-separated, because one experiment rarely needs two.
            properties=os.environ.get("CH32_BUILD_PROPERTY", "").split())
    except smoke.Failure as e:
        # Not a skip: a board is plugged in and something about the bench is
        # wrong - the wrong board named, two probes and no choice made, a
        # variant with no Serial. Those are findings.
        pytest.fail(str(e))


@pytest.fixture(scope="module")
def uart_routes(attached):
    """Which USART routes actually reach the host, measured once.

    Flashing a scan sketch takes most of a minute, and both assertions read the
    same answer, so it is done once for the module.
    """
    try:
        return uart_scan.scan(
            board=os.environ.get("CH32_BOARD"),
            pnum=os.environ.get("CH32_PNUM", "ANY"),
            port=os.environ.get("CH32_PORT"),
            probe=os.environ.get("CH32_PROBE"),
            baud=int(os.environ.get("CH32_BAUD", 115200)),
            seconds=float(os.environ.get("CH32_SECONDS", 12.0)))
    except smoke.Failure as e:
        pytest.fail(str(e))


def pytest_generate_tests(metafunc):
    """CH32_SKETCH decides how many cases there are, so it cannot be a fixture.

        CH32_SKETCH=all uv run pytest manual/smoke/smoke.py -v -s

    Default is smoke.DEFAULT_SKETCH, the one sketch worth running after any
    change; `all` is what to run after swapping a board onto the bench.
    """
    if "sketch_name" in metafunc.fixturenames:
        metafunc.parametrize("sketch_name", smoke.sketch_names(
            os.environ.get("CH32_SKETCH", smoke.DEFAULT_SKETCH)))
