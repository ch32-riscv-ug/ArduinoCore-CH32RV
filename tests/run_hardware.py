#!/usr/bin/env python3
"""Run the hardware cases on bench targets named by their addresses alone.

  uv run --project tests --locked --extra hardware --env-file tests/.env python tests/run_hardware.py [--execute]
  ... python tests/run_hardware.py --probe oep://30eda0e31108/x035 --probe wchlink://FB868F068059 --case gpio

A target is its address (CH32RV_PROBE in tests/.env, or --probe; oep://<unit>/<port>[/<target>] or
wchlink://<serial>): the probe, the chip and the probe's memo records say the rest (harness/place.py), and the cases
that can run there are chosen from those (each skipped one with its reason). Builds only, unless --execute."""
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

from harness.bench import CASES, PEER_CASES
from harness import place as places

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
    "wire": TESTS / "instrumented/wire/test_wire_contract.py",
    "softwire": TESTS / "instrumented/wire/test_wire_contract.py",
    "softspi": TESTS / "instrumented/spi/test_spi_contract.py",
    "spi": TESTS / "instrumented/spi/test_spi_contract.py",
    "sd": TESTS / "instrumented/sd/test_sd_contract.py",
    "periph": TESTS / "instrumented/periph/test_periph_contract.py",
    "signals": TESTS / "instrumented/signals/test_signals_contract.py",
    "system": TESTS / "instrumented/system/test_system_contract.py",
    "usb-peer": TESTS / "peer/usb/device/test_usb_peer_device.py",
    "usb-peer-classes": TESTS / "peer/usb/device/test_usb_peer_classes.py",
    "usb-peer-media": TESTS / "peer/usb/device/test_usb_peer_media.py",
    "usb-peer-net": TESTS / "peer/usb/device/test_usb_peer_net.py",
    "usb-peer-fs": TESTS / "peer/usb/device/test_usb_peer_fs.py",
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
    "wire": TESTS / "sketch_support/wire/wire.ino",
    "softwire": TESTS / "sketch_support/wire/wire.ino",
    "softspi": TESTS / "sketch_support/spi/spi.ino",
    "spi": TESTS / "sketch_support/spi/spi.ino",
    "sd": TESTS / "sketch_support/sd/sd.ino",
    "periph": TESTS / "sketch_support/periph/periph.ino",
    "signals": TESTS / "sketch_support/signals/signals.ino",
    "system": TESTS / "sketch_support/system/system.ino",
    "usb-peer": TESTS / "sketch_support/usb_contracts/usb_contracts.ino",
    "usb-peer-classes": TESTS / "sketch_support/usb_classes/usb_classes.ino",
    "usb-peer-media": TESTS / "sketch_support/usb_media/usb_media.ino",
    "usb-peer-net": TESTS / "sketch_support/usb_net/usb_net.ino",
    "usb-peer-fs": TESTS / "sketch_support/usb_fs/usb_fs.ino",
    "usb-peer-host": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
}
# build_opt.h lines staged beside a case's DUT sketch.
BUILD_OPTS = {
    # EspUsbHost 2.10.0 starts a bulk UVC stream but never reads it.
    "usb-peer-media": "-DUSB_MEDIA_ISOCHRONOUS=1\n",
    # X035 (a small series) has no format() by default; the test formats.
    "usb-peer-fs": "-DFF_USE_MKFS=1\n",
}
# The peer sketch directory staged beside a case's test (pytest-embedded-arduino-cli's peer_<name>/).
PEERS = {
    "usb-peer": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-classes": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-media": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-net": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-fs": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-host": TESTS / "sketch_support/usb_peer/host_hid/peer_s3",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer/host_classes/peer_s3",
}
HELPERS = {
    "usb": [TESTS / "instrumented/usb/usb_host.py"],
    "usb-classes": [TESTS / "instrumented/usb/usb_host.py"],
    "usb-media": [TESTS / "instrumented/usb/usb_host.py", TESTS / "instrumented/usb/usb_iso.py"],
    "gpio": [TESTS / "instrumented/bench_lines.py"],
    "uart": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "sd": [TESTS / "instrumented/bench_lines.py"],
    "periph": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "signals": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "system": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "spi": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "softspi": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "wire": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "softwire": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "usb-peer": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-classes": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-media": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-net": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-fs": [TESTS / "peer/usb/device/s3_peer.py"],
    "usb-peer-host": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-classes": [TESTS / "peer/usb/host/host_peer.py"],
}
PLUGINS = ["pytest_embedded.plugin",
           "pytest_embedded_arduino_cli.plugin", "pytest_embedded_arduino_cli_ch32rv.plugin",
           "pytest_embedded_wireskein.plugin"]


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


def junit_skips(path):
    """The skip messages when every test of the junit file skipped, else None (also when there is no file)."""
    import xml.etree.ElementTree as ET
    try:
        cases = list(ET.parse(path).getroot().iter("testcase"))
    except (OSError, ET.ParseError):
        return None
    skips = [c.find("skipped") for c in cases]
    if not cases or any(s is None for s in skips):
        return None
    return [s.get("message", "") for s in skips]


def stage_case(work, name, case, target):
    directory = work / "cases" / name / case
    directory.mkdir(parents=True)
    module = directory / MODULES[case].name
    shutil.copyfile(MODULES[case], module)
    for helper in HELPERS.get(case, []):
        shutil.copyfile(helper, directory / helper.name)
    if case in PEERS:
        shutil.copytree(PEERS[case], directory / "peer_s3")
        target = dict(target, usb_peer_port=places.peer_tty(target["usb_peer_serial"]))
    if case in BUILD_OPTS:
        (directory / "build_opt.h").write_text(BUILD_OPTS[case])
    build_id = uuid.uuid4().hex[:12]
    staged = {k: v for k, v in target.items() if k != "snapshot"}
    settings = dict(build_id=build_id, target=staged, f_cpu=target["f_cpu"], case=case)
    # The lines of gpio, spi, softspi, wire, softwire and sd come from the probe's labels (bench_lines.py).
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
    return module, build_id, target


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


def hid_bootloaders():
    root = pathlib.Path("/sys/bus/usb/devices")
    out = set()
    for dev in root.glob("*"):
        try:
            if (dev / "idVendor").read_text().strip() == "1209" and (dev / "idProduct").read_text().strip() == "b803":
                out.add(dev.name)
        except OSError:
            pass
    return out


def enter_hid(target):
    from oep_client import core, link, uiapduino
    before = hid_bootloaders()
    # The uploader's broker can retain the exclusive serial handle for a short idle period.
    deadline = time.monotonic() + 12
    while True:
        try:
            host = link.open_host(target["upload_port"])
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
    # The board's own USB is the bootloader's: the 1209:b803 device that appears now (none was there before).
    deadline = time.monotonic() + 10
    usb = None
    while time.monotonic() < deadline and usb is None:
        new = hid_bootloaders() - before
        if len(new) > 1:
            raise RuntimeError(f"several HID bootloaders appeared: {sorted(new)}")
        usb = pathlib.Path("/sys/bus/usb/devices") / new.pop() if new else None
        time.sleep(0.1)
    if usb is None:
        raise RuntimeError("the HID bootloader did not enumerate")
    target["soft_usb_topology"] = usb.name
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


def wait_for_probe(address, tool, env, logs, chip, timeout=60):
    """The place again, once the probe answers after a power cycle (it boots for some seconds)."""
    deadline = time.monotonic() + timeout
    while True:
        time.sleep(2)
        try:
            return places.resolve(address, tool, env, logs, chip=chip)
        except Exception:
            if time.monotonic() > deadline:
                raise


def parse_options(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--probe", action="append",
                        help="a target's address (repeat for several); otherwise CH32RV_PROBE (tests/.env)")
    parser.add_argument("--out", type=pathlib.Path, help="new run directory; otherwise auto-number under OEP_HW_RESULTS")
    parser.add_argument("--case", action="append", choices=sorted(CASES), help="default: every case the target can run")
    parser.add_argument("--execute", action="store_true", help="upload test sketches, observe and operate the probe's USB power")
    args = parser.parse_args(argv)
    if not args.probe:
        address = places.address_from_env()
        if not address:
            parser.error("--probe or CH32RV_PROBE (tests/.env) is required; no implicit equipment discovery")
        args.probe = [address]
    if len(set(a.lower() for a in args.probe)) != len(args.probe):
        parser.error("duplicate address")
    if args.out is None:
        root = os.environ.get("OEP_HW_RESULTS")
        if not root:
            parser.error("--out or OEP_HW_RESULTS is required")
        args.out = pathlib.Path(root) / ("core-" + time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "-" + uuid.uuid4().hex[:8])
    return args, parser


def main(argv=None):
    args, parser = parse_options(argv)
    chip = os.environ.get("CH32RV_CHIP")       # read before the isolated environment drops it (place.resolve)
    tool, gcc, versions = tool_paths()
    work = args.out.resolve()
    work.mkdir(parents=True, exist_ok=False)
    env = isolated_environment(work, tool, gcc)
    report = dict(execute=args.execute, tools=versions,
                  commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
                  dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO)), cases=[], probes={},
                  flash_backup=False, firmware_restore=False, probe_firmware_update=False, addresses=args.probe)
    packages = ("pytest", "pytest-embedded", "pytest-embedded-arduino-cli",
                "pytest-embedded-arduino-cli-ch32rv", "oep-client-python")
    report["host"] = dict(python=sys.version, packages={p: importlib.metadata.version(p) for p in packages},
        arduino_cli=invoke(["arduino-cli", "version"], work / "host/arduino-cli.log", env=env).strip(),
        ch32rv=json.loads(invoke([str(tool), "--json", "version"], work / "host/ch32rv.log", env=env)),
        uv_lock_sha256=hashlib.sha256((TESTS / "uv.lock").read_bytes()).hexdigest())
    resolved = {}

    def save():
        (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        (work / "places.json").write_text(json.dumps(
            {n: {k: v for k, v in p.items() if k != "snapshot"} for n, p in resolved.items()}, indent=2) + "\n")
    save()
    try:
        for address in args.probe:
            try:
                target = places.resolve(address, tool, env, work / "preflight" / address.replace("://", "-").replace("/", "-"),
                                        chip=chip)
            except Exception as error:
                name = address
                report["cases"].extend(dict(target=name, address=address, case=case, status="ENVIRONMENT_ERROR",
                                            error=str(error)) for case in (args.case or ["*"]))
                save()
                print(f"{address}: PREFLIGHT ERROR: {error}", flush=True)
                continue
            name = target["name"]
            if name in resolved:
                parser.error(f"two addresses name the target {name}")
            resolved[name] = target
            report["probes"][name] = target.get("snapshot")
            for warning in target["warnings"]:
                print(f"{name}: warning: {warning}", flush=True)
            wanted = args.case or [c for c, why in target["cases"].items() if why == "run"]
            for case in args.case or []:
                if target["cases"].get(case, "run") != "run":
                    report["cases"].append(dict(target=name, case=case, status="SKIP", reason=target["cases"][case][6:]))
            skipped = [c for c, why in target["cases"].items() if why != "run" and c not in wanted]
            report.setdefault("not_chosen", {})[name] = {c: target["cases"][c] for c in skipped}
            save()
            for case in wanted:
                if target["cases"].get(case, "run") != "run":
                    continue
                result = dict(target=name, case=case, status="NOT_RUN")
                report["cases"].append(result)
                save()
                try:
                    if case == "power" and not args.execute:
                        result.update(status="NOT_RUN", reason="power requires --execute")
                        continue
                    module, build_id, staged = stage_case(work, name, case, target)
                    result["build_id"] = build_id
                    case_env = dict(env, CH32_TEST_BUILD_ID=build_id)
                    if args.execute and (case == "hid" or (case != "power" and target.get("upload_port", "").startswith("ch32rv-hid://"))):
                        enter_hid(staged)
                    command = [sys.executable, "-m", "pytest", "-c", str(REPO / "pytest.ini")]
                    for plugin in PLUGINS:
                        command += ["-p", plugin]
                    command += [str(module), "--profile", name, "--port", staged["port"],
                                "--run-mode", "all" if args.execute else "build",
                                "--device-lock", "required", "--root-logdir", str(module.parent / "logs"),
                                "--junitxml", str(module.parent / "junit.xml"), "-o", "junit_family=xunit1", "-s", "-q"]
                    if case in PEER_CASES:
                        command += ["--peer-port", "s3:" + staged["usb_peer_port"]]
                    if case == "hid":
                        command += ["--flash-port", "ch32rv-hid://" + staged["soft_usb_topology"]]
                    elif staged.get("upload_port"):
                        command += ["--flash-port", staged["upload_port"]]
                    # A peer case builds the ESP32 sketch inside pytest too; on a loaded
                    # host that alone can take minutes.
                    invoke(command, module.parent / "pytest.log", env=case_env,
                           timeout=900 if case in PEER_CASES else 300)
                    result["status"] = "PASS" if args.execute else "COMPILE_PASS"
                    skips = junit_skips(module.parent / "junit.xml") if args.execute else None
                    if skips:
                        # Every test skipped (its lines are not labelled): not a pass.
                        result.update(status="SKIP", reason="; ".join(skips))
                    if args.execute and case == "power":
                        # The probe came back: the same chip is still on the port.
                        again = wait_for_probe(address, tool, env, module.parent / "after", chip)
                        if again["target_name"] != target["target_name"]:
                            raise RuntimeError(f"after the power cycle the port has {again['target_name']}")
                except Exception as error:
                    result.update(status="FAIL", error=str(error))
                    if args.execute and case == "power":
                        # Pass or fail, the next case must not start while the probe still boots.
                        try:
                            wait_for_probe(address, tool, env, module.parent / "after-fail", chip)
                        except Exception as again:
                            result["after"] = str(again)
                finally:
                    save()
                    print(f"{name}/{case}: {result['status']}", flush=True)
    finally:
        save()
    print(work / "result.json")
    return int(any(r["status"] in {"FAIL", "ENVIRONMENT_ERROR"} for r in report["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
