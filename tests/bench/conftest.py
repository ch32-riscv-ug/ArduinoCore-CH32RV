"""The bench tests' one precondition: the probe is the one the bench file describes.

bench/ is in norecursedirs, so this loads only for `pytest bench --profile <board>`. It adds
no device fixtures - those come from pytest-embedded-arduino-cli (dut, arduino_cli_app) and
pytest-embedded-arduino-cli-ch32rv (ch32rv, oep_host, ch32_uart). What it does is run
benchdef.check() once, after collection and before the first upload, and refuse the run
when the probe's firmware or settings differ from tests/benches/<name>.toml:

    ERROR: bench x035-p4 is not ready for --profile ch32x035:
      probe firmware: bench file says '0.0.5', probe says '3.2.0-v1rc'
    run: uv run --env-file .env tests/bench/prepare.py --profile ch32x035

That is a usage error (exit 4: the bench), not a test failure (the core), and it is
cheap: two lock-free reads, under a second. Re-flashing is prepare.py's job and is never
done from a test.

`bench` gives a test the checked bench file: wiring (`bench.channel("PA1")`), the DUT
UART the probe hears (`bench.uart`), measured facts.
"""
import os
import pathlib
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import benchdef  # noqa: E402

_BENCH = pytest.StashKey[dict]()


def pytest_collection_modifyitems(session, config, items):
    """One check per profile, before anything is uploaded. Only when a bench test was collected."""
    if not any(HERE in pathlib.Path(str(item.fspath)).parents for item in items):
        return
    profile = config.getoption("profile", None)
    if not profile:
        raise pytest.UsageError("bench tests need --profile <board>: the bench is chosen by profile")
    if config.getoption("run_mode", "all") == "build":
        return                                 # compiling only: no probe is touched, so none is checked
    try:
        bench = benchdef.load(profile, _port(config, profile))
        problems = benchdef.check(bench, benchdef.find_ch32rv())
    except benchdef.BenchError as e:
        bench, problems = None, [str(e)]
    if problems:
        name = bench.name if bench else "?"
        raise pytest.UsageError(
            f"bench {name} is not ready for --profile {profile}:\n  " + "\n  ".join(problems)
            + f"\nrun: uv run --env-file .env tests/bench/prepare.py --profile {profile}")
    config.stash.setdefault(_BENCH, {})[profile] = bench
    _compile_time_facts(bench)


def _compile_time_facts(bench) -> None:
    """The few bench facts a sketch needs before it can talk (build_config.toml <- environment): the boot
    marker pad, as the core's pin number."""
    pwm = bench.facts.get("pwm")
    if pwm:
        os.environ.setdefault("TEST_BENCH_MARK_PIN", str(_encode(pwm)))


def _encode(pad: str) -> int:
    """"PA0" -> the core's pin number, (port << 5) | bit (cores/arduino/ch32_pins.h, ADR-0010)."""
    import re
    m = re.fullmatch(r"P([A-F])(\d{1,2})", pad.strip().upper())
    if not m:
        raise pytest.UsageError(f"{pad!r} is not a pad name like PA0")
    return ("ABCDEF".index(m.group(1)) << 5) | int(m.group(2))


def pytest_report_header(config):
    benches = config.stash.get(_BENCH, {})
    return [f"bench: {b.name} ({b.probe.get('model')}, firmware {b.probe.get('firmware')}) for --profile {p}"
            for p, b in benches.items()]


@pytest.fixture(scope="module")
def bench(request: pytest.FixtureRequest) -> benchdef.Bench:
    """The bench file behind --profile (wiring, UART, facts), already checked against the probe."""
    if request.config.getoption("run_mode", "all") == "build":
        pytest.skip("build only: no bench")
    return request.config.stash[_BENCH][request.config.getoption("profile")]


def _port(config, profile: str) -> str:
    """The IDE port, the way pytest-embedded-arduino-cli resolves it: --port, else TEST_SERIAL_PORT_<PROFILE>."""
    import urllib.parse
    port = config.getoption("port", None) or os.environ.get(benchdef.env_key(profile, "SERIAL_PORT_")) or ""
    if port.startswith("arduinomonitor://"):
        port = urllib.parse.unquote(port[len("arduinomonitor://"):].split("?", 1)[0])
    return port

