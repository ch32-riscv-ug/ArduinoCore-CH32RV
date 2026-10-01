#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["oep-client-python>=0.0.24", "libusb1>=3", "pyusb>=1.2"]
# ///
"""Bring a bench's probe to the state its bench file describes, or check that it is there.

    uv run --env-file .env tests/bench/prepare.py --profile ch32x035            # erase, flash, configure, check
    uv run --env-file .env tests/bench/prepare.py --profile ch32x035 --check    # compare only (what pytest does first)
    uv run --env-file .env tests/bench/prepare.py --profile ch32x035 --config   # write slot / binds / plan, save, check

The bench file is tests/benches/<name>.toml (TEST_BENCH_<PROFILE>); the probe is the one
behind TEST_SERIAL_PORT_<PROFILE>. The steps, for an OEP probe:

  1. put the oep-probe-arduino release the file names on the probe. Two routes, `[probe] update`:
       "usj" (default): fetch the tag's tarball, build examples/<example> with the sketch.yaml
              in that tag, erase the probe's whole flash and upload through its bootloader
              port (TEST_BENCH_<PROFILE>_UPLOAD). The first flash of a blank probe, always.
       "dfu": fetch the release's app image (firmware-<ver>.json names it, with its sha256)
              and send it over USB DFU to the running probe (bench/dfu.py), which verifies it
              and reboots into it. The HS port is all this needs - the permanent P4 jigs
              expose nothing else. `--usj` forces the first route.
     A tests-only step either way: a released probe firmware is what a bench runs, never
     a working tree.
  2. write the probe's settings from the file (erase the stored ones first), save.
  3. read them back and compare, with the firmware / model / profile from describe and
     the storage state (the probe runs on what it saved).

A WCH-Link bench has only step 3: its firmware is WCH's tools' to update.

Nothing here runs from a test. tests/bench/conftest.py runs step 3 and, on a mismatch,
tells the person to run this. Firmware is flashed on purpose, by hand, when a version is
raised or a probe is in an unknown state - and then it is flashed whole, from erase.
"""
from __future__ import annotations

import argparse
import io
import re
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
    # The release sketch.yaml names the library by version from the Library Manager, which lags a release by
    # hours; the tag's own src/ is that version, so build against the tree (what the release's firmware
    # workflow does too).
    yaml = sketch / "sketch.yaml"
    lib = os.path.relpath(tree, sketch)          # ../.. for examples/<X>, ../../.. for examples/Firmware/<X>
    text = re.sub(r"^(\s*)- OpenEmbeddedProbe \([^)]*\)\s*$", rf"\1- dir: {lib}", yaml.read_text(encoding="utf-8"), flags=re.M)
    yaml.write_text(text, encoding="utf-8")
    build = work / "build"
    # An example with one profile per probe chip (examples/Firmware/OepProbe: rp2040, rp2350, esp32p4, esp32)
    # names the jig's in [probe] sketch_profile; one with a single profile leaves it out.
    profile = ["--profile", probe["sketch_profile"]] if probe.get("sketch_profile") else []
    # The release sketch.yaml pins the library by version from the Library Manager, so the
    # local indexes must know that version (the guide's rule: update before a pinned build).
    log("updating the package and library indexes")
    run(["arduino-cli", "core", "update-index"])
    run(["arduino-cli", "lib", "update-index"])
    log(f"building {sketch.relative_to(tree)} with its release sketch.yaml (--clean{' ' + ' '.join(profile) if profile else ''})")
    run(["arduino-cli", "compile", "--clean", *profile, "--build-path", str(build), str(sketch)], cwd=sketch)
    port = upload_port(bench)
    if probe.get("erase") == "all":
        erase_flash(bench, sketch, build, port, profile)
    log(f"uploading to {port}")
    run(["arduino-cli", "upload", *profile, "--build-path", str(build), "-p", port, str(sketch)], cwd=sketch)
    reattach_usbip(bench)


def release_asset(tag: str, name: str, into: pathlib.Path) -> pathlib.Path:
    url = f"{PROBE_REPO}/releases/download/{tag}/{name}"
    log(f"fetching {url}")
    into.mkdir(parents=True, exist_ok=True)
    path = into / name
    path.write_bytes(urllib.request.urlopen(url, timeout=120).read())
    return path


def flash_dfu(bench: benchdef.Bench, work: pathlib.Path) -> None:
    """The release's app image over USB DFU to the running probe: no bootloader port, no build."""
    import hashlib
    import json
    import dfu
    probe = bench.probe
    tag = probe["release"]
    version = tag.lstrip("v")
    manifest = json.loads(release_asset(tag, f"firmware-{version}.json", work / "release").read_text(encoding="utf-8"))
    entry = next((e for e in manifest["firmware"]
                  if e.get("example") == probe["example"] and e.get("profile") == probe.get("sketch_profile")
                  and e.get("kind") == "app"), None)
    if entry is None:
        raise SystemExit(f"{tag}'s firmware-{version}.json has no app image for {probe['example']} / "
                         f"{probe.get('sketch_profile')}; the DFU route needs oep-probe-arduino >= 0.0.16")
    image = release_asset(tag, entry["file"], work / "release").read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != entry["sha256"]:
        raise SystemExit(f"{entry['file']}: sha256 {digest} is not the release's {entry['sha256']}")
    # DFU writes the other app slot and leaves the settings; erase them here so step 2 starts clean, as the
    # whole-flash erase of the usj route does. Best effort: the firmware being replaced may speak an older wire
    # than this client (0.0.21 -> 0.0.22), and step 2 erases again on the new firmware anyway.
    try:
        hst, close = benchdef.open_probe(bench, None)
        try:
            from oep_client import config
            hst.open(lease_ms=10000, owner="prepare")
            log("erasing the stored settings")
            cfg = config.ProbeConfig(hst)
            cfg.erase()
            cfg.save()
            hst.end()
        finally:
            close()
    except Exception as e:   # noqa: BLE001 - see above
        log(f"could not erase the old firmware's settings ({e}); step 2 erases them on the new one")
    log(f"sending {entry['file']} ({len(image)} bytes) over DFU to {bench.probe_serial}")
    try:
        dfu.download(bench.probe_serial, image, log)
    except dfu.DfuError as e:
        raise SystemExit(f"DFU: {e}") from None
    dfu.wait_reboot(bench.probe_serial, log, back_s=5.0)
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


def erase_flash(bench: benchdef.Bench, sketch: pathlib.Path, build: pathlib.Path, port: str, profile: list) -> None:
    """Whole-chip erase before the upload, with the platform's own tool (esptool for the ESP32s). RP2040/RP2350
    probes are erased by their flash_nuke UF2, which is a drive copy this script does not do yet."""
    props = show_properties(sketch, profile)
    esptool = props.get("runtime.tools.esptool_py.path")
    if not esptool:
        raise SystemExit("this probe's platform has no esptool (runtime.tools.esptool_py.path); erase by hand")
    exe = next((p for p in (pathlib.Path(esptool) / "esptool", pathlib.Path(esptool) / "esptool.py") if p.exists()), None)
    cmd = [str(exe)] if exe and exe.suffix != ".py" else [sys.executable, str(exe)]
    chip = props.get("build.mcu", "auto")
    log(f"erasing the whole flash ({chip}) on {port}")
    run(cmd + ["--chip", chip, "--port", port, "erase_flash"])


def show_properties(sketch: pathlib.Path, profile: list = ()) -> dict[str, str]:
    out = run(["arduino-cli", "compile", "--show-properties=expanded", *profile, str(sketch)], cwd=sketch, capture=True)
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
    ap.add_argument("--usj", action="store_true",
                    help="flash through the bootloader port even when the bench file says update = \"dfu\" "
                         "(a blank probe, or one whose firmware predates DFU)")
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
            if bench.probe.get("update") == "dfu" and not args.usj:
                flash_dfu(bench, work)
            else:
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
