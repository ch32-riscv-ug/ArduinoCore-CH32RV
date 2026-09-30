#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["oep-client-python>=0.0.5", "libusb1>=3"]
# ///
"""Bring a bench's probe to the state its bench file describes, or check that it is there.

    uv run --env-file .env tests/bench/prepare.py --profile ch32x035            # erase, flash, configure, check
    uv run --env-file .env tests/bench/prepare.py --profile ch32x035 --check    # compare only (what pytest does first)
    uv run --env-file .env tests/bench/prepare.py --profile ch32x035 --config   # write slot / binds / plan, save, check

The bench file is tests/benches/<name>.toml (TEST_BENCH_<PROFILE>); the probe is the one
behind TEST_SERIAL_PORT_<PROFILE>. The steps, for an OEP probe:

  1. fetch the oep-probe-arduino release the file names (its tag's tarball), build
     examples/<example> with the sketch.yaml in that tag (it pins the platform and the
     library), erase the probe's whole flash and upload. A tests-only step: a released
     probe firmware is what a bench runs, never a working tree.
  2. write the probe's settings from the file (erase the stored ones first), save.
  3. read them back and compare, with the firmware / model / profile from describe.

A WCH-Link bench has only step 3: its firmware is WCH's tools' to update.

Nothing here runs from a test. tests/bench/conftest.py runs step 3 and, on a mismatch,
tells the person to run this. Firmware is flashed on purpose, by hand, when a version is
raised or a probe is in an unknown state - and then it is flashed whole, from erase.
"""
from __future__ import annotations

import argparse
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import benchdef  # noqa: E402

PROBE_REPO = "https://github.com/Open-Embedded-Probe/oep-probe-arduino"


def log(msg: str) -> None:
    print(msg, flush=True)


# ---------------------------------------------------------------- step 1: firmware

def fetch_release(tag: str, into: pathlib.Path) -> pathlib.Path:
    """The tag's source tree, from GitHub's tarball. -> the tree's root."""
    url = f"{PROBE_REPO}/archive/refs/tags/{tag}.tar.gz"
    log(f"fetching {url}")
    data = urllib.request.urlopen(url, timeout=120).read()
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        tar.extractall(into, filter="data")
    roots = [p for p in into.iterdir() if p.is_dir()]
    if len(roots) != 1:
        raise SystemExit(f"unexpected tarball layout under {into}: {roots}")
    return roots[0]


def upload_port(bench: benchdef.Bench) -> str:
    """The serial port the probe's own bootloader is reached through (the ESP32's USB-Serial/JTAG, the RP's
    CDC): TEST_BENCH_<PROFILE>_UPLOAD in .env, since it is this machine's device path."""
    key = benchdef.env_key(bench.profile, "BENCH_") + "_UPLOAD"
    port = os.environ.get(key)
    if not port:
        raise SystemExit(f"{key} is not set: the serial port the probe is flashed through (e.g. its USB-Serial/JTAG)")
    return port


def flash_firmware(bench: benchdef.Bench, work: pathlib.Path) -> None:
    probe = bench.probe
    tree = fetch_release(probe["release"], work / "release")
    sketch = tree / "examples" / probe["example"]
    if not (sketch / "sketch.yaml").exists():
        raise SystemExit(f"{sketch} has no sketch.yaml in {probe['release']}")
    build = work / "build"
    # The release sketch.yaml pins the library by version from the Library Manager, so the
    # local indexes must know that version (the guide's rule: update before a pinned build).
    log("updating the package and library indexes")
    run(["arduino-cli", "core", "update-index"])
    run(["arduino-cli", "lib", "update-index"])
    log(f"building {sketch.relative_to(tree)} with its release sketch.yaml (--clean)")
    run(["arduino-cli", "compile", "--clean", "--build-path", str(build), str(sketch)], cwd=sketch)
    port = upload_port(bench)
    if probe.get("erase") == "all":
        erase_flash(bench, sketch, build, port)
    log(f"uploading to {port}")
    run(["arduino-cli", "upload", "--build-path", str(build), "-p", port, str(sketch)], cwd=sketch)
    reattach_usbip(bench)


def reattach_usbip(bench: benchdef.Bench) -> None:
    """A WSL bench: the probe's USB device re-enumerates after a flash and usbipd drops it, so attach it again
    when TEST_BENCH_<PROFILE>_USBIP_BUSID names it. A bench on a Linux host has nothing to do here."""
    import time
    busid = os.environ.get(benchdef.env_key(bench.profile, "BENCH_") + "_USBIP_BUSID")
    if not busid or not shutil.which("usbipd.exe"):
        return
    time.sleep(4)
    log(f"re-attaching usbipd busid {busid}")
    subprocess.run(["usbipd.exe", "attach", "--wsl", "--busid", busid], text=True)
    time.sleep(4)


def erase_flash(bench: benchdef.Bench, sketch: pathlib.Path, build: pathlib.Path, port: str) -> None:
    """Whole-chip erase before the upload, with the platform's own tool (esptool for the ESP32s). RP2040/RP2350
    probes are erased by their flash_nuke UF2, which is a drive copy this script does not do yet."""
    props = show_properties(sketch)
    esptool = props.get("runtime.tools.esptool_py.path")
    if not esptool:
        raise SystemExit("this probe's platform has no esptool (runtime.tools.esptool_py.path); erase by hand")
    exe = next((p for p in (pathlib.Path(esptool) / "esptool", pathlib.Path(esptool) / "esptool.py") if p.exists()), None)
    cmd = [str(exe)] if exe and exe.suffix != ".py" else [sys.executable, str(exe)]
    chip = props.get("build.mcu", "auto")
    log(f"erasing the whole flash ({chip}) on {port}")
    run(cmd + ["--chip", chip, "--port", port, "erase_flash"])


def show_properties(sketch: pathlib.Path) -> dict[str, str]:
    out = run(["arduino-cli", "compile", "--show-properties=expanded", str(sketch)], cwd=sketch, capture=True)
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


# ---------------------------------------------------------------- step 2: settings

def write_config(bench: benchdef.Bench, ch32rv) -> None:
    from oep_client import config
    hst, close = benchdef.open_probe(bench, ch32rv)
    try:
        hst.open(lease_ms=10000, owner="prepare")
        cfg = config.ProbeConfig(hst)
        items = benchdef.wanted_items(bench, hst)
        log("erasing the stored settings")
        cfg.erase()
        log(f"writing {len(items)} items: " + ", ".join(type(i).__name__ for i in items))
        cfg.set(items)
        h = cfg.save()
        log(f"saved: hash {h:#x}")
        hst.end()
    finally:
        close()


# ---------------------------------------------------------------- step 3: check

def check(bench: benchdef.Bench, ch32rv) -> int:
    problems = benchdef.check(bench, ch32rv)
    if problems:
        log(f"bench {bench.name} does not match --profile {bench.profile}:")
        for p in problems:
            log(f"  {p}")
        return 1
    log(f"bench {bench.name} is ready for --profile {bench.profile} ({bench.probe.get('model')}, "
        f"firmware {bench.probe.get('firmware')})")
    return 0


# ---------------------------------------------------------------- plumbing

def run(cmd: list[str], cwd=None, capture=False) -> str:
    log("  $ " + " ".join(cmd))
    r = subprocess.run(cmd, cwd=cwd, text=True, capture_output=capture)
    if r.returncode:
        raise SystemExit(f"{cmd[0]} failed ({r.returncode})" + (f":\n{r.stderr}" if capture else ""))
    return r.stdout if capture else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--profile", required=True, help="the board / bench, e.g. ch32x035")
    ap.add_argument("--check", action="store_true", help="only compare the probe with the bench file")
    ap.add_argument("--config", action="store_true", help="write the settings and check; do not flash")
    ap.add_argument("--keep", action="store_true", help="keep the work directory")
    args = ap.parse_args()
    try:
        bench = benchdef.load(args.profile)
    except benchdef.BenchError as e:
        log(str(e))
        return 2
    ch32rv = benchdef.find_ch32rv()
    if args.check:
        return check(bench, ch32rv)
    if bench.probe.get("kind") == "wchlink":
        log("a WCH-Link bench has nothing to prepare; checking")
        return check(bench, ch32rv)
    work = pathlib.Path(tempfile.mkdtemp(prefix="bench-prepare-"))
    try:
        if not args.config:
            flash_firmware(bench, work)
            log("waiting for the probe to come back")
            import time
            time.sleep(4)
        write_config(bench, ch32rv)
        return check(bench, ch32rv)
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
