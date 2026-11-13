"""The Arduino workspace a hardware run builds in: the pinned ch32rv and toolchain (tools/index) and an
isolated Arduino user directory whose ch32-riscv-ug:ch32rv platform is this work tree (run_hardware.py and the
pytest bench plugin, harness/pytest_bench.py)."""
import json
import os
import pathlib
import shutil
import subprocess
import sys

from harness import place as places

REPO = pathlib.Path(__file__).resolve().parents[2]


def tool_paths():
    versions = {}
    for name, metadata in [("ch32rv", "tools_ch32rv.json"), ("toolchain", "tools_ch32rv_toolchain.json")]:
        versions[name] = json.loads((REPO / "tools/index" / metadata).read_text())["version"]
    tool = REPO / ".tools/ch32rv" / versions["ch32rv"] / "ch32rv"
    # CH32_TEST_CH32RV: another ch32rv binary (one built ahead of its release) in place of the pinned one; the
    # results record it as `ch32rv: <path>`.
    other = os.environ.get("CH32_TEST_CH32RV", "").strip()
    if other:
        tool = pathlib.Path(other).resolve()
        versions["ch32rv"] = str(tool)
    # CH32_TOOLCHAIN_BIN: another toolchain's bin/ (one built ahead of its release) in place of the pinned one; the
    # results record it as `toolchain: <path>`.
    toolchain = REPO / ".tools/ch32rv-toolchain" / versions["toolchain"] / "bin"
    other = os.environ.get("CH32_TOOLCHAIN_BIN", "").strip()
    if other:
        toolchain = pathlib.Path(other).resolve()
        versions["toolchain"] = str(toolchain)
    if not tool.is_file() or not any((toolchain / n).is_file() for n in ("clang", "clang.exe")):
        raise ValueError("fetch pinned ch32rv and ch32rv-toolchain first (tools/index/fetch_tools.py)")
    return tool, toolchain, versions


def isolated_environment(work, tool, toolchain):
    env = dict(os.environ)
    for key in ("CH32RV_PROBE", "CH32RV_CHIP", "CH32RV_DB", "CH32RV_REPLAY", "CH32RV_CAPTURE", "ARDUINO_CONFIG_FILE"):
        env.pop(key, None)
    for key in [k for k in env if k.startswith(places.LINK_RECORD_PREFIX)]:
        env.pop(key)
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
    # Its indexes are refreshed every run: a peer's sketch.yaml raised to a
    # version newer than the cached index fails with "Library ... not found"
    # (EspUsbDevice 3.2.0, 2026-11-12). Offline, the cached ones stay.
    try:
        subprocess.run(["arduino-cli", "update"], env=env, capture_output=True, timeout=120, check=False)
    except subprocess.TimeoutExpired:
        pass
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
    env["PATH"] = str(bin_dir) + os.pathsep + str(toolchain) + os.pathsep + env.get("PATH", "")
    platform = work / "user/hardware/ch32-riscv-ug/ch32rv"
    platform.mkdir(parents=True)
    for name in ("cores", "variants", "libraries", "platform.txt", "boards.txt", "programmers.txt"):
        (platform / name).symlink_to(REPO / name, target_is_directory=(REPO / name).is_dir())
    (platform / "platform.local.txt").write_text(
        f"compiler.path={toolchain}{os.sep}\n"
        f"runtime.tools.ch32rv.path={tool.parent}\n"
        f"tools.ch32rv.path={tool.parent}\n"
        "tools.ch32rv.cmd=ch32rv\n"
    )
    return env
