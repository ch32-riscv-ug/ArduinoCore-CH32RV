#!/usr/bin/env python3
"""Compile contracts by default; explicitly execute against a host-local bench with --execute."""
import argparse
import fcntl
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

from harness.bench import CASES, check_identity, check_target, load_bench
from harness.provenance import oep_snapshot

REPO = pathlib.Path(__file__).resolve().parents[1]
TESTS = REPO / "tests"
MODULES = {
    "runtime": TESTS / "single/runtime/test_runtime_contract.py",
    "uart": TESTS / "instrumented/uart/test_uart_contract.py",
    "gpio": TESTS / "instrumented/gpio/test_gpio_contract.py",
    "hid": TESTS / "single/runtime/test_runtime_contract.py",
    "power": TESTS / "instrumented/power/test_power_contract.py",
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
    for name in ("user", "data", "downloads"):
        env[f"ARDUINO_DIRECTORIES_{name.upper()}"] = str(work / name)
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
    build_id = uuid.uuid4().hex[:12]
    settings = dict(build_id=build_id, target=target, f_cpu=target["f_cpu"])
    if case == "gpio":
        settings["gpio"] = [dict(p, pin=((ord(p["pad"][1]) - ord("A") + 2) << 5) | int(p["pad"][2:])) for p in target["gpio"]]
    (directory / "case.json").write_text(json.dumps(settings, indent=2))
    if case != "power":
        sketch = TESTS / "sketch_support/core_contracts/core_contracts.ino"
        shutil.copyfile(sketch, directory / f"{case}.ino")
        # JSON strings are valid YAML scalars. No platform version is requested: use the isolated worktree platform.
        (directory / "sketch.yaml").write_text(
            f"profiles:\n  {name}:\n    fqbn: {json.dumps(target['fqbn'])}\n"
            + ("    protocol: hid\n" if case == "hid" or target.get("upload_port", "").startswith("hid://") else "")
            +
            "    port_config:\n      source: dmseq\n"
            f"      chip: {target['chip']}\n"
        )
        (directory / "build_config.toml").write_text('[defines]\nCH32_TEST_BUILD_ID = "BENCH_BUILD_ID"\n')
    return module, build_id


def target_info(tool, target, log, env):
    info = json.loads(invoke([str(tool), "--probe", f"port:{target['port']}", "--chip", target["chip"],
                             "--non-interactive", "--json", "target", "info"], log, env=env))
    return check_target(target, info)


def record_probe(tool, target, directory, env):
    directory.mkdir(parents=True, exist_ok=True)
    try:
        if target["usb_vid"] == "1a86" and target["usb_pid"] == "8010":
            reported = json.loads(invoke([str(tool), "--probe", f"port:{target['port']}",
                "--non-interactive", "--json", "probe", "info"], directory / "probe-info.log", env=env))
            if not reported.get("ok"):
                raise RuntimeError("probe.info did not succeed")
            snapshot = reported.get("probe") or reported.get("result", {})
        else:
            snapshot = oep_snapshot(target["port"])
        snapshot["metadata_status"] = "reported" if snapshot.get("firmware") else "missing_firmware"
    except Exception as error:
        snapshot = dict(metadata_status="unavailable", firmware=None, error=str(error))
    (directory / "probe.json").write_text(json.dumps(snapshot, indent=2) + "\n")
    return snapshot


def enter_hid(target):
    from oep_client import core, link, riscv, uiapduino
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
        uiapduino.enter_bootloader(host, riscv.Wire(host, "oep.wire.swio"), gpio, target["nrst_channel"])
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True, help="new artifact directory")
    parser.add_argument("--target", action="append", help="default: all configured targets")
    parser.add_argument("--case", action="append", choices=sorted(CASES), help="default: each target's declared cases")
    parser.add_argument("--execute", action="store_true", help="back up flash, upload test sketches and operate configured USB power")
    args = parser.parse_args()
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
                  dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO)), cases=[], probes={})
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
    lock = None
    try:
        if args.execute:
            lock_path = pathlib.Path(bench["lock_file"])
            lock_path.parent.mkdir(parents=True, exist_ok=True)
            lock = lock_path.open("a")
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for name in targets:
            target = bench["targets"][name]
            cases = args.case or target["cases"]
            if args.execute:
                try:
                    check_identity(target)
                    report["probes"][name] = record_probe(tool, target, work / f"preflight/{name}", env)
                    save()
                    info = target_info(tool, target, work / f"preflight/{name}/info.log", env)
                    # Read and retain user flash before the first upload. The system bootloader is not written.
                    size = info["flash_bytes"]
                    backup = work / f"preflight/{name}/flash-before.bin"
                    invoke([str(tool), "--probe", f"port:{target['port']}", "--chip", target["chip"], "--non-interactive",
                            "read", "--range", f"0x08000000+{size}", "-o", str(backup)], backup.with_suffix(".log"), env=env)
                    if backup.stat().st_size != size:
                        raise RuntimeError("incomplete user flash backup")
                    (backup.parent / "backup.json").write_text(json.dumps(dict(chip=target["chip"], size=size,
                        sha256=hashlib.sha256(backup.read_bytes()).hexdigest()), indent=2))
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
                        if case == "hid" or (case != "power" and target.get("upload_port", "").startswith("hid://")):
                            enter_hid(target)
                    command = [sys.executable, "-m", "pytest", "-c", str(REPO / "pytest.ini")]
                    for plugin in PLUGINS:
                        command += ["-p", plugin]
                    command += [str(module), "--profile", name, "--port", target["port"],
                                "--run-mode", "all" if args.execute else "build",
                                "--device-lock", "required", "--root-logdir", str(module.parent / "logs"),
                                "--junitxml", str(module.parent / "junit.xml"), "-o", "junit_family=xunit1", "-s", "-q"]
                    if case == "hid":
                        command += ["--flash-port", "hid://" + target["soft_usb_topology"]]
                    elif target.get("upload_port"):
                        command += ["--flash-port", target["upload_port"]]
                    invoke(command, module.parent / "pytest.log", env=case_env, timeout=300)
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
        if lock:
            lock.close()
        save()
    print(work / "result.json")
    return int(any(r["status"] in {"FAIL", "ENVIRONMENT_ERROR"} for r in report["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
