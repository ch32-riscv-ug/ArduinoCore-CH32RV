"""The bench tests' one precondition: the probe is the one the bench file describes.

bench/ is in norecursedirs, so this loads only for `pytest bench --profile <board>`. It adds
no device fixtures - those come from pytest-embedded-arduino-cli (dut, arduino_cli_app) and
pytest-embedded-arduino-cli-ch32rv (ch32rv, oep_host, ch32_uart). What it does is run
benchdef.check() once, after collection and before the first upload, and refuse the run
when the probe's firmware or settings differ from tests-legacy/benches/<name>.toml:

    ERROR: bench x035-p4 is not ready for --profile ch32x035:
      probe firmware: bench file says '0.0.5', probe says '3.2.0-v1rc'
    run: uv run --env-file .env tests-legacy/bench/prepare.py --profile ch32x035

That is a usage error (exit 4: the bench), not a test failure (the core), and it is
cheap: two lock-free reads, under a second. Re-flashing is prepare.py's job and is never
done from a test.

`bench` gives a test the checked bench file: wiring (`bench.channel("PA1")`), the DUT
UART the probe hears (`bench.uart`), measured facts.

The bench builds the working tree, not a release. The profiles name the platform without a
version, and for the session this repository is linked into the sketchbook as
<user>/hardware/ch32-riscv-ug/ch32rv (host-arduino-core's way); the toolchain and ch32rv come
from <data>/packages, put there once by bench/install_tools.py, which is checked here too.
"""
import os
import pathlib
import shutil
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import benchdef  # noqa: E402
import install_tools  # noqa: E402

_BENCH = pytest.StashKey[dict]()


def pytest_collection_modifyitems(session, config, items):
    """One check per profile, before anything is uploaded. Only when a bench test was collected."""
    if not any(HERE in pathlib.Path(str(item.fspath)).parents for item in items):
        return
    profile = config.getoption("profile", None)
    if not profile:
        raise pytest.UsageError("bench tests need --profile <board>: the bench is chosen by profile")
    problems = install_tools.check()
    if problems:
        raise pytest.UsageError("the working tree cannot be built as the platform:\n  " + "\n  ".join(problems)
                                + "\nrun: uv run tests-legacy/bench/install_tools.py")
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
            + f"\nrun: uv run --env-file .env tests-legacy/bench/prepare.py --profile {profile}")
    config.stash.setdefault(_BENCH, {})[profile] = bench
    _compile_time_facts(bench)


def _compile_time_facts(bench) -> None:
    """The few bench facts a sketch needs before it can talk (build_config.toml <- environment): the boot
    marker pad as the core's pin number, and the UART a sketch may put its console on."""
    pwm = bench.facts.get("pwm")
    if pwm:
        os.environ.setdefault("TEST_BENCH_MARK_PIN", str(_encode(pwm)))
    uart = bench.data.get("uart")
    if uart:                                   # a sketch whose console is that UART (bench/regs/reg_probe)
        os.environ.setdefault("TEST_BENCH_CONSOLE_UART", str(uart["usart"]))
        os.environ.setdefault("TEST_BENCH_CONSOLE_ROUTE", str(uart["route"]))


def _encode(pad: str) -> int:
    """"PA0" -> the core's pin number (tracekit.encode: cores/arduino/ch32rv_pins.h, ADR-0010)."""
    import tracekit
    try:
        return tracekit.encode(pad)
    except ValueError as e:
        raise pytest.UsageError(str(e)) from None


@pytest.fixture(scope="session", autouse=True)
def _working_tree_platform():
    """The repository as <user>/hardware/ch32-riscv-ug/ch32rv for the session, so a version-less profile
    resolves to it. A link that already points here is reused and left; anything else there is an error
    (install_tools.check has said so). Where the OS refuses a symlink, the release entries are copied."""
    link = install_tools.platform_link()
    made = ""
    if link.is_symlink() and link.resolve() == REPO.resolve():
        pass
    elif link.exists() or link.is_symlink():
        pytest.fail(f"{link} exists and is not a link to {REPO}")
    else:
        link.parent.mkdir(parents=True, exist_ok=True)
        try:
            link.symlink_to(REPO, target_is_directory=True)
            made = "symlink"
        except OSError:
            sys.path.insert(0, str(REPO / "tools" / "index"))
            import install_check
            install_check.stage_platform_copy(link)
            made = "copy"
    yield link
    if made == "symlink" and link.is_symlink():
        link.unlink()
    elif made == "copy" and link.exists():
        shutil.rmtree(link)
    if made:
        try:
            link.parent.rmdir()
        except OSError:
            pass


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
    """The IDE port, the way pytest-embedded-arduino-cli resolves it: --port, else TEST_SERIAL_PORT_<PROFILE>. A
    platform-monitor port arrives wrapped (arduinomonitor://...); the plugin's MonitorTarget takes the wrapper off
    (its public API since 1.8.0 - the URL's spelling is not a contract)."""
    from pytest_embedded_arduino_cli import MonitorTarget, is_monitor_url
    port = config.getoption("port", None) or os.environ.get(benchdef.env_key(profile, "SERIAL_PORT_")) or ""
    if is_monitor_url(port):
        port = MonitorTarget.from_url(port).address
    return port
