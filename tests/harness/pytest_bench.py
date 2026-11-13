"""The bench's hooks and fixtures for the hardware tests in tests/hw, in pytest-embedded-arduino-cli's own terms
(its guide, TESTING_ADVANCED: profiles, --combo, .env). tests/hw/conftest.py imports this module and nothing else.

A test names the coverage.toml case it belongs to (`pytestmark = pytest.mark.ch32_case("gpio")`); whether it can run
on a profile is decided in two steps:

1. At collection, from the configuration only (no equipment): the profile's fqbn names the part, and the part's
   series has the case or not (coverage.toml, the variants) - pytest_arduino_cli_skip_reason.
2. Before a test's fixtures, from the equipment: the port the profile resolves to (TEST_SERIAL_PORT_<PROFILE>) is
   read once a session (harness/place.py: the probe's labels and records, a WCH-Link's CH32RV_PROBE_RECORD_<serial>,
   ch32rv's capabilities, the chip), and the case runs there or is skipped with the reason.

The other conftest jobs the guide allows: the workspace (the work tree's core in an isolated Arduino user
directory, the pinned ch32rv and toolchain - "prepare the environment before the run") and the build id, a define
each build carries so a test sees the DUT runs that build and not an older one."""
import hashlib
import os
import pathlib
import re
import secrets
import time

import pytest
import yaml

from harness import place as places

_STATE = pytest.StashKey[dict]()
_BUILD_ID = pytest.StashKey[str]()
_PROFILE = pytest.StashKey[str]()
_PLACE = pytest.StashKey[dict]()


def pytest_configure(config):
    config.addinivalue_line("markers", "ch32_case(name): the coverage.toml case a hardware test belongs to "
                                       "(harness/pytest_bench.py decides where it can run)")
    config.stash[_STATE] = dict(nonce=secrets.token_hex(4), places={}, workspace=None)


def case_of(item):
    marker = item.get_closest_marker("ch32_case")
    return marker.args[0] if marker else None


def _combination(item):
    from pytest_embedded_arduino_cli import combination as combinations
    return item.stash.get(combinations.COMBINATION, None)


def profile_fqbn(test_path, profile):
    """The fqbn of `profile` in the sketch.yaml beside the test."""
    data = yaml.safe_load((pathlib.Path(test_path).parent / "sketch.yaml").read_text(encoding="utf-8")) or {}
    return ((data.get("profiles") or {}).get(profile) or {}).get("fqbn")


def part_of(fqbn):
    m = re.search(r"[:,]pnum=([A-Z0-9]+)", fqbn or "")
    return m.group(1) if m else None


# 1. collection: the configuration only ----------------------------------------------------------------------------

def pytest_arduino_cli_skip_reason(item, combination):
    case = case_of(item)
    if case is None or combination is None or combination.primary is None:
        return None
    part = part_of(profile_fqbn(item.path, combination.primary))
    if part is None:
        return f"profile {combination.primary}: its fqbn names no part (pnum=)"
    return places.static_reason(case, part)


# 2. before the fixtures: the equipment ----------------------------------------------------------------------------

def workspace(config):
    """The isolated Arduino environment, made once a session and put in os.environ (arduino-cli, which the plugin
    runs, inherits it)."""
    state = config.stash[_STATE]
    if state["workspace"] is None:
        from harness.workspace import isolated_environment, tool_paths
        tool, toolchain, versions = tool_paths()
        root = pathlib.Path(os.environ.get("OEP_HW_RESULTS") or config.rootpath / "work").resolve()
        work = root / ("hw-" + time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "-" + state["nonce"])
        work.mkdir(parents=True)
        env = isolated_environment(work, tool, toolchain)
        env.pop("PYTEST_DISABLE_PLUGIN_AUTOLOAD", None)
        link_records = places.link_records_from_env()   # read before the environment drops them
        for key in set(os.environ) - set(env):
            del os.environ[key]
        os.environ.update(env)
        state["workspace"] = dict(work=work, tool=tool, toolchain=toolchain, versions=versions, env=env,
                                  link_records=link_records)
    return state["workspace"]


def profile_of(item):
    combination = _combination(item)
    if combination is not None:
        return combination.primary
    return item.config.getoption("profile", None)


def port_of_profile(profile):
    from pytest_embedded_arduino_cli.serial import normalize_profile_name
    return os.environ.get(f"TEST_SERIAL_PORT_{normalize_profile_name(profile)}") if profile else None


def place_of(config, port, part):
    """The place behind `port`, read once a session: the probe, its records, the chip (harness/place.py)."""
    state = config.stash[_STATE]
    key = places.canonical(port)
    if key not in state["places"]:
        ws = workspace(config)
        logs = ws["work"] / "preflight" / key.replace("://", "-").replace("/", "-")
        found = places.resolve(port, ws["tool"], ws["env"], logs, chip=part,
                               link_record=ws["link_records"].get(key))
        if found["chip"] != part:
            raise places.PlaceError(f"{port}: the profile's part is {part}, the chip there is {found['chip']}")
        state["places"][key] = found
    return state["places"][key]


@pytest.hookimpl(tryfirst=True)
def pytest_runtest_setup(item):
    case = case_of(item)
    if case is None:
        return
    config = item.config
    ws = workspace(config)
    profile = profile_of(item)
    item.stash[_PROFILE] = profile
    # The build id: one per sketch and profile in this session (the plugin builds once a module and combination).
    combination = _combination(item)
    key = f"{config.stash[_STATE]['nonce']}|{item.path}|{combination.id if combination else profile}"
    build_id = hashlib.sha1(key.encode()).hexdigest()[:12]
    os.environ["CH32_TEST_BUILD_ID"] = ws["env"]["CH32_TEST_BUILD_ID"] = build_id
    item.stash[_BUILD_ID] = build_id
    if config.getoption("run_mode", "all") == "build":
        return
    port = port_of_profile(profile)
    if not port:
        return   # the plugin skips: the primary port is not resolved
    part = part_of(profile_fqbn(item.path, profile))
    place = place_of(config, port, part)
    item.stash[_PLACE] = place
    why = place["cases"].get(case, "run")
    if why != "run":
        pytest.skip(why.removeprefix("skip: "))


@pytest.fixture
def ch32_target(request):
    """The place the test runs on (harness/place.py resolve: lines, pads, records, the chip, the probe's
    capabilities) with what a case adds (the pads to keep off, the build id)."""
    item = request.node
    place = item.stash.get(_PLACE, None)
    if place is None:
        pytest.skip("no place: build-only run, or the profile's port is not set")
    target = {k: v for k, v in place.items() if k != "snapshot"}
    target["build_id"] = item.stash[_BUILD_ID]
    target["profile"] = item.stash[_PROFILE]
    case = case_of(item)
    if case in ("selfcheck", "selfbus", "selfsys"):
        target["gpio_keep_off"] = places.gpio_keep_off(target["chip"])
    if case == "selfsys":
        target["selfsys_pins"] = places.gpio_pads(target["chip"])
    return target
