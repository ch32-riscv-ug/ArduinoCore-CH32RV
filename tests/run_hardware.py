#!/usr/bin/env python3
"""Compile contracts by default; explicitly execute against a host-local bench with --execute."""
import argparse
import hashlib
import importlib.metadata
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time
import uuid

from harness.bench import CASES, PEER_CASES, check_identity, check_target, check_probe, load_bench
from harness.provenance import oep_snapshot

REPO = pathlib.Path(__file__).resolve().parents[1]
TESTS = REPO / "tests"
MODULES = {
    "runtime": TESTS / "single/runtime/test_runtime_contract.py",
    "uart": TESTS / "instrumented/uart/test_uart_contract.py",
    "gpio": TESTS / "instrumented/gpio/test_gpio_contract.py",
    "hid": TESTS / "single/runtime/test_runtime_contract.py",
    "power": TESTS / "instrumented/power/test_power_contract.py",
    "usb": TESTS / "instrumented/usb/test_usb_contract.py",
    "usb-classes": TESTS / "instrumented/usb/test_usb_classes_contract.py",
    "usb-media": TESTS / "instrumented/usb/test_usb_media_contract.py",
    "eth": TESTS / "instrumented/eth/test_eth_contract.py",
    "logic": TESTS / "instrumented/logic/test_logic_contract.py",
    "pd": TESTS / "instrumented/pd/test_pd_contract.py",
    "synckit": TESTS / "instrumented/synckit/test_synckit_contract.py",
    "flash": TESTS / "instrumented/flash/test_flash_contract.py",
    "usb-peer": TESTS / "peer/usb/device/test_usb_peer_device.py",
    "usb-peer-classes": TESTS / "peer/usb/device/test_usb_peer_classes.py",
    "usb-peer-media": TESTS / "peer/usb/device/test_usb_peer_media.py",
    "usb-peer-net": TESTS / "peer/usb/device/test_usb_peer_net.py",
    "usb-peer-host": TESTS / "peer/usb/host/hid/test_usb_peer_host.py",
    "usb-peer-host-classes": TESTS / "peer/usb/host/classes/test_usb_peer_host_classes.py",
}
# Sketches other than core_contracts, and helper modules staged beside a case's test.
SKETCHES = {
    "usb": TESTS / "sketch_support/usb_contracts/usb_contracts.ino",
    "usb-classes": TESTS / "sketch_support/usb_classes/usb_classes.ino",
    "usb-media": TESTS / "sketch_support/usb_media/usb_media.ino",
    "eth": TESTS / "sketch_support/eth/eth.ino",
    "logic": TESTS / "sketch_support/logic/logic.ino",
    "pd": TESTS / "sketch_support/pd/pd.ino",
    "synckit": TESTS / "sketch_support/synckit/synckit.ino",
    "flash": TESTS / "sketch_support/flash/flash.ino",
    "usb-peer": TESTS / "sketch_support/usb_contracts/usb_contracts.ino",
    "usb-peer-classes": TESTS / "sketch_support/usb_classes/usb_classes.ino",
    "usb-peer-media": TESTS / "sketch_support/usb_media/usb_media.ino",
    "usb-peer-net": TESTS / "sketch_support/usb_net/usb_net.ino",
    "usb-peer-host": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
}
# build_opt.h lines staged beside a case's DUT sketch.
BUILD_OPTS = {
    # EspUsbHost 2.10.0 starts a bulk UVC stream but never reads it.
    "usb-peer-media": "-DUSB_MEDIA_ISOCHRONOUS=1\n",
}
# The peer sketch directory staged beside a case's test (pytest-embedded-arduino-cli's peer_<name>/).
PEERS = {
    "usb-peer": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-classes": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-media": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-net": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-host": TESTS / "sketch_support/usb_peer/host_hid/peer_s3",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer/host_classes/peer_s3",
}
HELPERS = {
    "usb": [TESTS / "instrumented/usb/usb_host.py"],
    "usb-classes": [TESTS / "instrumented/usb/usb_host.py"],
    "usb-media": [TESTS / "instrumented/usb/usb_host.py", TESTS / "instrumented/usb/usb_iso.py"],
    "usb-peer": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-classes": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-media": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-net": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-host": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-classes": [TESTS / "peer/usb/host/host_peer.py"],
}
PLUGINS = ["pytest_embedded.plugin",
           "pytest_embedded_arduino_cli.plugin", "pytest_embedded_arduino_cli_ch32rv.plugin"]


def invoke(command, log, *, env=None, timeout=180):
    log.parent.mkdir(parents=True, exist_ok=True)
    log.with_suffix(".command.json").write_text(json.dumps(command, indent=2))
    with log.open("w") as stream:
        result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"exit {result.returncode}: see {log}")
    return log.read_text()


def tool_paths():
    versions = {}
    for name, metadata in [("ch32rv", "tools_ch32rv.json"), ("gcc", "tools_xpack_gcc.json")]:
        versions[name] = json.loads((REPO / "tools/index" / metadata).read_text())["version"]
    tool = REPO / ".tools/ch32rv" / versions["ch32rv"] / "ch32rv"
    gcc = REPO / ".tools/xpack-riscv-none-elf-gcc" / versions["gcc"] / "bin"
    if not tool.is_file() or not (gcc / "riscv-none-elf-gcc").is_file():
        raise ValueError("fetch pinned ch32rv and xpack-riscv-none-elf-gcc first (tools/index/fetch_tools.py)")
    return tool, gcc, versions


def isolated_environment(work, tool, gcc):
    env = dict(os.environ)
    for key in ("CH32RV_PROBE", "CH32RV_CHIP", "CH32RV_DB", "CH32RV_REPLAY", "CH32RV_CAPTURE", "ARDUINO_CONFIG_FILE"):
        env.pop(key, None)
    env["ARDUINO_DIRECTORIES_USER"] = str(work / "user")
    # Packages a profile installs (an ESP32 peer's pinned core and libraries)
    # live in a cache of the bench's own, kept between runs: a fresh one would
    # download the ESP32 toolchains every time. This core never comes from
    # there - it is the work tree, linked under user/hardware below - and
    # ~/.arduino15 is not touched.
    cache = pathlib.Path(os.environ.get("CH32_TEST_ARDUINO_CACHE",
                                        pathlib.Path.home() / ".cache/ch32rv-core-bench/arduino"))
    for name in ("data", "downloads"):
        (cache / name).mkdir(parents=True, exist_ok=True)
        env[f"ARDUINO_DIRECTORIES_{name.upper()}"] = str(cache / name)
    env["PYTEST_DISABLE_PLUGIN_AUTOLOAD"] = "1"
    # A short discovery timeout can fall back to serial before OEP/HID add events arrive.
    # Keep the standard plugin lifecycle and give the real CLI time to discover typed ports.
    cli = shutil.which("arduino-cli")
    if not cli:
        raise ValueError("arduino-cli is required")
    bin_dir = work / "bin"
    bin_dir.mkdir()
    wrapper = bin_dir / "arduino-cli"
    wrapper.write_text(f"#!{sys.executable}\nimport os, sys\ncommand = {cli!r}\n"
                       "args = sys.argv[1:]\n"
                       "if args and args[0] == 'upload' and '--discovery-timeout' not in args:\n"
                       "    args += ['--discovery-timeout', '10s']\n"
                       "os.execv(command, [command, *args])\n")
    wrapper.chmod(0o755)
    env["PATH"] = str(bin_dir) + os.pathsep + str(gcc) + os.pathsep + env.get("PATH", "")
    platform = work / "user/hardware/ch32-riscv-ug/ch32rv"
    platform.mkdir(parents=True)
    for name in ("cores", "variants", "libraries", "platform.txt", "boards.txt", "programmers.txt"):
        (platform / name).symlink_to(REPO / name, target_is_directory=(REPO / name).is_dir())
    (platform / "platform.local.txt").write_text(
        f"compiler.path={gcc}{os.sep}\n"
        f"runtime.tools.ch32rv.path={tool.parent}\n"
        f"tools.ch32rv.path={tool.parent}\n"
        "tools.ch32rv.cmd=ch32rv\n"
    )
    return env


def stage_case(work, name, case, target):
    directory = work / "cases" / name / case
    directory.mkdir(parents=True)
    module = directory / MODULES[case].name
    shutil.copyfile(MODULES[case], module)
    for helper in HELPERS.get(case, []):
        shutil.copyfile(helper, directory / helper.name)
    if case in PEERS:
        shutil.copytree(PEERS[case], directory / "peer_s3")
    if case in BUILD_OPTS:
        (directory / "build_opt.h").write_text(BUILD_OPTS[case])
    build_id = uuid.uuid4().hex[:12]
    settings = dict(build_id=build_id, target=target, f_cpu=target["f_cpu"])
    if case == "gpio":
        settings["gpio"] = [dict(p, pin=((ord(p["pad"][1]) - ord("A") + 2) << 5) | int(p["pad"][2:])) for p in target["gpio"]]
    (directory / "case.json").write_text(json.dumps(settings, indent=2))
    if case != "power":
        sketch = SKETCHES.get(case, TESTS / "sketch_support/core_contracts/core_contracts.ino")
        shutil.copyfile(sketch, directory / f"{case}.ino")
        # JSON strings are valid YAML scalars. No platform version is requested: use the isolated worktree platform.
        (directory / "sketch.yaml").write_text(
            f"profiles:\n  {name}:\n    fqbn: {json.dumps(target['fqbn'])}\n"
            + ("    protocol: ch32rv-hid\n" if case == "hid" or target.get("upload_port", "").startswith("ch32rv-hid://") else "")
            +
            "    port_config:\n      source: dmseq\n"
            f"      chip: {target['chip']}\n"
        )
        (directory / "build_config.toml").write_text('[defines]\nCH32_TEST_BUILD_ID = "BENCH_BUILD_ID"\n')
    return module, build_id


def probe_address(target):
    """How ch32rv names the target's probe: an OEP probe by its port (oep://<probe>/<port>, which a probe
    with several ports needs), a WCH-Link by its serial path."""
    upload = target.get("upload_port", "")
    return upload if upload.startswith("oep://") else target["port"]


def target_info(tool, target, log, env):
    info = json.loads(invoke([str(tool), "--probe", probe_address(target), "--chip", target["chip"],
                             "--non-interactive", "--json", "target", "info"], log, env=env))
    return check_target(target, info)


def record_probe(tool, target, directory, env):
    directory.mkdir(parents=True, exist_ok=True)
    try:
        if target["usb_vid"] == "1a86" and target["usb_pid"] == "8010":
            reported = json.loads(invoke([str(tool), "--probe", target["port"],
                "--non-interactive", "--json", "probe", "info"], directory / "probe-info.log", env=env))
            if not reported.get("ok"):
                raise RuntimeError("probe.info did not succeed")
            snapshot = reported["probe"]
        else:
            snapshot = oep_snapshot(target["port"])
        snapshot["metadata_status"] = "reported" if snapshot.get("firmware") else "missing_firmware"
    except Exception as error:
        snapshot = dict(metadata_status="unavailable", firmware=None, error=str(error))
    (directory / "probe.json").write_text(json.dumps(snapshot, indent=2) + "\n")
    return snapshot


def swio_wire(host):
    """oep.wire.swio on the probe's one named port (host guide §5.3: the pins come from the port's saved
    labels, `S.swio`; since the reworked spec an attach names its pins). uiapduino attaches without pins,
    so they are put on the wire's attach."""
    from oep_client import riscv, settings
    labels = settings.labels(host)
    ports = settings.ports_named(labels)
    swio = settings.line_from_labels(labels, "swio", ports[0] if len(ports) == 1 else None)
    if swio is None:
        raise RuntimeError(f"no single swio line in the probe's labels (ports {ports}): label it <port>.swio")
    wire = riscv.Wire(host, "oep.wire.swio")
    attach = wire.attach
    wire.attach = lambda *args, **kw: attach(*args, **{"pins": (swio, 0xFFFF), **kw})
    return wire


def enter_hid(target):
    from oep_client import core, link, uiapduino
    # The uploader's broker can retain the exclusive serial handle for a short idle period.
    deadline = time.monotonic() + 12
    while True:
        try:
            host = link.open_host(target["port"])
            break
        except link.PortBusy:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)
    try:
        core.take(host, 30000, owner="ArduinoCore HID contract")
        gpio = core.find(host, "oep.fixture.gpio")
        core.plan_apply(host, [(gpio, 1, target["nrst_channel"])])
        uiapduino.enter_bootloader(host, swio_wire(host), gpio, target["nrst_channel"])
    finally:
        try:
            host.end()
        finally:
            host.link.close()
    usb = pathlib.Path("/sys/bus/usb/devices") / target["soft_usb_topology"]
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if (usb / "idProduct").exists() and (usb / "idProduct").read_text().strip() == "b803":
            break
        time.sleep(0.1)
    if not (usb / "idVendor").exists() or (usb / "idVendor").read_text().strip() != "1209" or (usb / "idProduct").read_text().strip() != "b803":
        raise RuntimeError("expected HID bootloader did not enumerate on the configured USB topology")
    # Enumeration precedes udev permission assignment: wait for an accessible matching node.
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        for node in pathlib.Path("/sys/class/hidraw").glob("hidraw*"):
            if usb.resolve() in (node / "device").resolve().parents:
                try:
                    descriptor = os.open("/dev/" + node.name, os.O_RDWR | os.O_NONBLOCK)
                except (PermissionError, FileNotFoundError):
                    continue
                os.close(descriptor)
                return
        time.sleep(0.1)
    raise RuntimeError("HID bootloader has no accessible hidraw node after udev settling")


def parse_options(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench", type=pathlib.Path, default=os.environ.get("CH32_TEST_CONFIG"), help="explicit config; otherwise CH32_TEST_CONFIG")
    parser.add_argument("--out", type=pathlib.Path, help="new run directory; otherwise auto-number under OEP_HW_RESULTS")
    parser.add_argument("--target", action="append", help="default: all configured targets")
    parser.add_argument("--case", action="append", choices=sorted(CASES), help="default: each target's declared cases")
    parser.add_argument("--execute", action="store_true", help="upload test sketches, observe and operate explicitly configured USB power")
    args = parser.parse_args(argv)
    if args.bench is None:
        parser.error("--bench or CH32_TEST_CONFIG is required; no implicit equipment discovery")
    if args.out is None:
        root = os.environ.get("OEP_HW_RESULTS")
        if not root:
            parser.error("--out or OEP_HW_RESULTS is required")
        args.out = pathlib.Path(root) / ("core-" + time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "-" + uuid.uuid4().hex[:8])
    return args, parser


def main(argv=None):
    args, parser = parse_options(argv)
    bench = load_bench(args.bench)
    targets = args.target or list(bench["targets"])
    if len(set(targets)) != len(targets) or set(targets) - bench["targets"].keys():
        parser.error("unknown or duplicate target")
    for name in targets:
        if args.case and set(args.case) - set(bench["targets"][name]["cases"]):
            parser.error(f"requested case is not wired/supported on {name}")
    tool, gcc, versions = tool_paths()
    work = args.out.resolve()
    work.mkdir(parents=True, exist_ok=False)
    env = isolated_environment(work, tool, gcc)
    report = dict(execute=args.execute, tools=versions,
                  commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
                  dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO)), cases=[], probes={},
                  flash_backup=False, firmware_restore=False, probe_firmware_update=False,
                  configuration_path=str(args.bench.resolve()),
                  configuration_sha256=hashlib.sha256(args.bench.read_bytes()).hexdigest())
    packages = ("pytest", "pytest-embedded", "pytest-embedded-arduino-cli",
                "pytest-embedded-arduino-cli-ch32rv", "oep-client-python")
    report["host"] = dict(python=sys.version, packages={p: importlib.metadata.version(p) for p in packages},
        arduino_cli=invoke(["arduino-cli", "version"], work / "host/arduino-cli.log", env=env).strip(),
        ch32rv=json.loads(invoke([str(tool), "--json", "version"], work / "host/ch32rv.log", env=env)),
        uv_lock_sha256=hashlib.sha256((TESTS / "uv.lock").read_bytes()).hexdigest())
    (work / "bench.json").write_text(json.dumps(bench, indent=2))
    def save():
        (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    save()
    try:
        for name in targets:
            target = bench["targets"][name]
            cases = args.case or target["cases"]
            if args.execute:
                try:
                    check_identity(target)
                    report["probes"][name] = record_probe(tool, target, work / f"preflight/{name}", env)
                    check_probe(target, report["probes"][name])
                    save()
                    target_info(tool, target, work / f"preflight/{name}/info.log", env)
                except Exception as error:
                    report["cases"].extend(dict(target=name, case=case, status="ENVIRONMENT_ERROR", error=str(error)) for case in cases)
                    save()
                    print(f"{name}: PREFLIGHT ERROR: {error}", flush=True)
                    continue
            for case in cases:
                result = dict(target=name, case=case, status="NOT_RUN")
                report["cases"].append(result)
                save()
                try:
                    if case == "power" and not args.execute:
                        result.update(status="NOT_RUN", reason="power requires --execute")
                        continue
                    module, build_id = stage_case(work, name, case, target)
                    result["build_id"] = build_id
                    case_env = dict(env, CH32_TEST_BUILD_ID=build_id)
                    if args.execute:
                        check_identity(target)
                        if case == "hid" or (case != "power" and target.get("upload_port", "").startswith("ch32rv-hid://")):
                            enter_hid(target)
                    command = [sys.executable, "-m", "pytest", "-c", str(REPO / "pytest.ini")]
                    for plugin in PLUGINS:
                        command += ["-p", plugin]
                    # An OEP probe's port goes by its oep:// address: a probe with several ports opens none by its serial path.
                    port = target["upload_port"] if target.get("upload_port", "").startswith("oep://") else target["port"]
                    command += [str(module), "--profile", name, "--port", port,
                                "--run-mode", "all" if args.execute else "build",
                                "--device-lock", "required", "--root-logdir", str(module.parent / "logs"),
                                "--junitxml", str(module.parent / "junit.xml"), "-o", "junit_family=xunit1", "-s", "-q"]
                    if case in PEER_CASES:
                        command += ["--peer-port", "s3:" + target["usb_peer_port"]]
                    if case == "hid":
                        command += ["--flash-port", "ch32rv-hid://" + target["soft_usb_topology"]]
                    elif target.get("upload_port"):
                        command += ["--flash-port", target["upload_port"]]
                    # A peer case builds the ESP32 sketch inside pytest too; on a loaded
                    # host that alone can take minutes.
                    invoke(command, module.parent / "pytest.log", env=case_env,
                           timeout=900 if case in PEER_CASES else 300)
                    result["status"] = "PASS" if args.execute else "COMPILE_PASS"
                    if args.execute and case == "power":
                        # Wait for board-identify, then check the target through the reopened connection.
                        time.sleep(2)
                        check_identity(target)
                        target_info(tool, target, module.parent / "info-after.log", env)
                except Exception as error:
                    result.update(status="FAIL", error=str(error))
                finally:
                    save()
                    print(f"{name}/{case}: {result['status']}", flush=True)
    finally:
        save()
    print(work / "result.json")
    return int(any(r["status"] in {"FAIL", "ENVIRONMENT_ERROR"} for r in report["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
