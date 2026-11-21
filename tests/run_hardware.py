#!/usr/bin/env python3
"""Run the hardware cases on bench targets named by their addresses alone.

  uv run --project tests --locked --extra hardware --env-file tests/.env python tests/run_hardware.py [--execute]
  ... python tests/run_hardware.py --probe oep://80f1b2d835d6/x035c8t6-pico --probe wchlink://A94B8F062EE9 --case gpio

A target is its address (CH32RV_PROBE in tests/.env, or --probe; oep://<unit>/<port>[/<target>] or
wchlink://<serial>): the probe, the chip and the probe's memo records say the rest (harness/place.py), and the cases
that can run there are chosen from those (each skipped one with its reason). Builds only, unless --execute."""
import argparse
import hashlib
import importlib.metadata
import json
import re
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import time
import uuid

from harness.bench import CASES, PEER_CASES
from harness import place as places
from harness.workspace import isolated_environment, tool_paths

REPO = pathlib.Path(__file__).resolve().parents[1]
TESTS = REPO / "tests"
MODULES = {
    "runtime": TESTS / "single/runtime/test_runtime_contract.py",
    "uart": TESTS / "instrumented/uart/test_uart_contract.py",
    "uart-dma": TESTS / "instrumented/uart/test_uart_dma.py",
    "software-serial": TESTS / "instrumented/software_serial/test_software_serial.py",
    "uart-sync": TESTS / "instrumented/uart/test_uart_sync.py",
    "pioc": TESTS / "instrumented/pioc/test_pioc.py",
    "gpio": TESTS / "instrumented/gpio/test_gpio_contract.py",
    "hid": TESTS / "single/runtime/test_runtime_contract.py",
    "power": TESTS / "instrumented/power/test_power_contract.py",
    "usb": TESTS / "instrumented/usb/test_usb_contract.py",
    "usb-classes": TESTS / "instrumented/usb/test_usb_classes_contract.py",
    "usb-media": TESTS / "instrumented/usb/test_usb_media_contract.py",
    "usb-ss-iso": TESTS / "instrumented/usb/test_usb_ss_iso.py",
    "eth": TESTS / "instrumented/eth/test_eth_contract.py",
    "eth-libs": TESTS / "instrumented/eth/test_eth_libs.py",
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
    "analog": TESTS / "instrumented/analog/test_analog_contract.py",
    "selfcheck": TESTS / "instrumented/selfcheck/test_selfcheck_contract.py",
    "selfbus": TESTS / "instrumented/selfbus/test_selfbus_contract.py",
    "selfsys": TESTS / "instrumented/selfsys/test_selfsys_contract.py",
    "selfapi": TESTS / "instrumented/selfapi/test_selfapi_contract.py",
    "selfexti": TESTS / "instrumented/selfexti/test_selfexti_contract.py",
    "console-rtt": TESTS / "instrumented/console/test_console_contract.py",
    "console-dmdata": TESTS / "instrumented/console/test_console_contract.py",
    "console-sdi": TESTS / "instrumented/console/test_console_contract.py",
    "usb-peer": TESTS / "peer/usb/device/test_usb_peer_device.py",
    "usb-peer-classes": TESTS / "peer/usb/device/test_usb_peer_classes.py",
    "usb-peer-media": TESTS / "peer/usb/device/test_usb_peer_media.py",
    "usb-peer-net": TESTS / "peer/usb/device/test_usb_peer_net.py",
    "usb-peer-fs": TESTS / "peer/usb/device/test_usb_peer_fs.py",
    "usb-peer-firmware": TESTS / "peer/usb/device/test_usb_peer_firmware.py",
    "usb-peer-input": TESTS / "peer/usb/device/test_usb_peer_input.py",
    "usb-peer-host": TESTS / "peer/usb/host/hid/test_usb_peer_host.py",
    "usb-peer-host-classes": TESTS / "peer/usb/host/classes/test_usb_peer_host_classes.py",
    "usb-peer-host-input": TESTS / "peer/usb/host/input/test_usb_peer_host_input.py",
    "usb-peer-host-storage": TESTS / "peer/usb/host/storage/test_usb_peer_host_storage.py",
    "usb-peer-host-fs": TESTS / "peer/usb/host/storage/test_usb_peer_host_fs.py",
    "usb-peer-host-power": TESTS / "peer/usb/host/power/test_usb_peer_host_power.py",
}
# Sketches other than core_contracts, and helper modules staged beside a case's test.
# The console cases: the monitor's source (port_config), and the flag that makes the sketch use that console.
CONSOLE_SOURCES = {"console-rtt": "rtt", "console-dmdata": "dmdata", "console-sdi": "sdi"}
# The console of every case goes to the probe's UART lines (<port>.tx / .rx, the fixture UART) where the target has
# them: Serial on those pins (bench_console.h, CONSOLE_UART_RX / _TX) and the monitor on fixture-uart. The DMSeq monitor
# polls the debug wire all the time, and on V407 that put +-80 counts on the DAC's PA4 line beside it (+-4 with the
# console on the UART, the monitor open; 2026-11-21). These stay on DMSeq: the cases that test the USARTs or the probe's
# UARTs (uart*, periph, the self* sketches that run every HardwareSerial, software-serial and pioc against the fixture
# UART), selfsys (it drives every pad, the console's too, and sleeps) and the console cases (the console is what they
# test).
DMSEQ_CONSOLE_CASES = ("uart", "uart-dma", "uart-sync", "periph", "selfcheck", "selfbus", "selfapi", "selfsys",
                       "software-serial", "pioc", *CONSOLE_SOURCES)
# Parts this small keep DMSeq too: Serial's code and buffers did not fit V003F4P6's 16 KB / 2 KB beside logic's and
# wire's sketches (2026-11-21).
UART_CONSOLE_MIN_RAM = 4096
# The test sketches' console header, staged beside each one.
BENCH_CONSOLE = TESTS / "sketch_support/common/bench_console.h"


def uart_console(case, target):
    """(DUT RX pad, DUT TX pad) for a case whose console goes to the probe's UART lines, else None."""
    lines, pads = target.get("lines") or {}, target.get("pads") or {}
    if case in DMSEQ_CONSOLE_CASES or "tx" not in lines or "rx" not in lines:
        return None
    # CH32H41x on the V5F (the CPU Core menu's default) hangs under the USART's interrupts - running, no longer
    # haltable - after a few hundred to a few thousand replies; the V3F and DMSeq do not (TODO, 2026-11-21).
    if target.get("family", "").startswith("CH32H41") and "core=v3f" not in target.get("fqbn", ""):
        return None
    ram = places.part_ram(target.get("chip", ""))      # None: a RAM split menu sets it (V30x / V407 / V317: large)
    if ram is not None and ram < UART_CONSOLE_MIN_RAM:
        return None
    pad_of = {v: k for k, v in pads.items()}
    rx, tx = pad_of.get(lines["tx"]), pad_of.get(lines["rx"])     # the probe's tx reaches the DUT's RX
    return (rx.upper(), tx.upper()) if rx and tx else None

# Libraries from the Arduino library index a case's DUT sketch uses, pinned (its sketch.yaml's profile installs
# them into the bench's Arduino cache): eth-libs runs widely used network libraries unchanged on this core.
CASE_LIBRARIES = {"eth-libs": ("PubSubClient (2.8)", "ArduinoMqttClient (0.1.8)", "ArduinoHttpClient (0.6.2)",
                               "aWOT (3.5.0)")}
# A case's own build flag (build_config.toml [flags]: a #define): the console it runs on, or the part of a shared
# sketch only that case builds (usb-peer-host-fs: the FS library, 15-33 KB, which the 64 KB parts' other host
# cases cannot carry).
CONSOLE_FLAGS = {"console-rtt": "CONSOLE_RTT", "console-dmdata": "CONSOLE_DMDATA", "console-sdi": "CONSOLE_SDI",
                 "usb-peer-host-fs": "USB_HOST_FS"}
SKETCHES = {
    "usb": TESTS / "sketch_support/usb_contracts/usb_contracts.ino",
    "usb-classes": TESTS / "sketch_support/usb_classes/usb_classes.ino",
    "usb-media": TESTS / "sketch_support/usb_media/usb_media.ino",
    "usb-ss-iso": TESTS / "sketch_support/usb_ss_iso/usb_ss_iso.ino",
    "eth": TESTS / "sketch_support/eth/eth.ino",
    "eth-libs": TESTS / "sketch_support/eth_libs/eth_libs.ino",
    "logic": TESTS / "sketch_support/logic/logic.ino",
    "pd": TESTS / "sketch_support/pd/pd.ino",
    "synckit": TESTS / "sketch_support/synckit/synckit.ino",
    "flash": TESTS / "sketch_support/flash/flash.ino",
    "uart": TESTS / "sketch_support/uart/uart.ino",
    "uart-dma": TESTS / "sketch_support/uart_dma/uart_dma.ino",
    "software-serial": TESTS / "sketch_support/software_serial/software_serial.ino",
    "uart-sync": TESTS / "sketch_support/uart_sync/uart_sync.ino",
    "pioc": TESTS / "sketch_support/pioc/pioc.ino",
    "wire": TESTS / "sketch_support/wire/wire.ino",
    "softwire": TESTS / "sketch_support/wire/wire.ino",
    "softspi": TESTS / "sketch_support/spi/spi.ino",
    "spi": TESTS / "sketch_support/spi/spi.ino",
    "sd": TESTS / "sketch_support/sd/sd.ino",
    "periph": TESTS / "sketch_support/periph/periph.ino",
    "signals": TESTS / "sketch_support/signals/signals.ino",
    "system": TESTS / "sketch_support/system/system.ino",
    "analog": TESTS / "sketch_support/analog/analog.ino",
    "selfcheck": TESTS / "sketch_support/selfcheck/selfcheck.ino",
    "selfbus": TESTS / "sketch_support/selfbus/selfbus.ino",
    "selfsys": TESTS / "sketch_support/selfsys/selfsys.ino",
    "selfapi": TESTS / "sketch_support/selfapi/selfapi.ino",
    "selfexti": TESTS / "sketch_support/selfexti/selfexti.ino",
    "console-rtt": TESTS / "sketch_support/console/console.ino",
    "console-dmdata": TESTS / "sketch_support/console/console.ino",
    "console-sdi": TESTS / "sketch_support/console/console.ino",
    "usb-peer": TESTS / "sketch_support/usb_contracts/usb_contracts.ino",
    "usb-peer-classes": TESTS / "sketch_support/usb_classes/usb_classes.ino",
    "usb-peer-media": TESTS / "sketch_support/usb_media/usb_media.ino",
    "usb-peer-net": TESTS / "sketch_support/usb_net/usb_net.ino",
    "usb-peer-fs": TESTS / "sketch_support/usb_fs/usb_fs.ino",
    "usb-peer-firmware": TESTS / "sketch_support/usb_firmware/usb_firmware.ino",
    "usb-peer-input": TESTS / "sketch_support/usb_input/usb_input.ino",
    "usb-peer-host": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-input": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-storage": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-fs": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
    "usb-peer-host-power": TESTS / "sketch_support/usb_peer_host/usb_peer_host.ino",
}
# The record's usb ports (harness.records.USB_PORTS) as the test sketches' controller (USB_TEST_CONTROLLER) and host port
# (USB_PEER_HOST_PORT).
USB_CONTROLLERS = {"full_speed": "FullSpeed", "high_speed": "HighSpeed", "super_speed": "SuperSpeed", "usbd": "Usbd",
                   "high_speed_2": "HighSpeed2"}
USB_HOST_PORTS = {p: "CH32RV_USB_HOST_PORT_" + p.upper()
                  for p in ("full_speed", "high_speed", "high_speed_2", "super_speed")}
# build_opt.h lines staged beside a case's DUT sketch.
BUILD_OPTS = {
    # EspUsbHost 2.10.0 starts a bulk UVC stream but never reads it.
    "usb-peer-media": "-DUSB_MEDIA_ISOCHRONOUS=1\n",
    # X035 (a small series) has no format() by default; the test formats.
    "usb-peer-fs": "-DFF_USE_MKFS=1\n",
    # Rewrites the whole program flash: a part-written image stays reachable from the probe.
    "usb-peer-firmware": "-DCH32RV_BOOT_HOLD_MS=1000\n",
}
# The peer sketch directory staged beside a case's test (pytest-embedded-arduino-cli's peer_<name>/).
PEERS = {
    "usb-peer": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-classes": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-media": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-net": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-fs": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-firmware": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-input": TESTS / "sketch_support/usb_peer/device/peer_s3",
    "usb-peer-host": TESTS / "sketch_support/usb_peer/host_hid/peer_s3",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer/host_classes/peer_s3",
    "usb-peer-host-power": TESTS / "sketch_support/usb_peer/host_hid/peer_s3",
}
# The same cases with an RP2040 (Raspberry Pi Pico) as the peer: on the DUT's own OEP probe, written over SWD and
# talked to over SEGGER RTT (oep-client's pytest plugin writes it through the upload hook), staged as peer_pico/.
PICO_PEERS = {
    "usb-peer": TESTS / "sketch_support/usb_peer/device/peer_pico",
    "usb-peer-classes": TESTS / "sketch_support/usb_peer/device/peer_pico",
    "usb-peer-net": TESTS / "sketch_support/usb_peer/device/peer_pico",
    "usb-peer-fs": TESTS / "sketch_support/usb_peer/device/peer_pico",
    "usb-peer-firmware": TESTS / "sketch_support/usb_peer/device/peer_pico",
    "usb-peer-input": TESTS / "sketch_support/usb_peer/device/peer_pico",
    "usb-peer-host": TESTS / "sketch_support/usb_peer/host_hid/peer_pico",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer/host_classes/peer_pico",
    "usb-peer-host-input": TESTS / "sketch_support/usb_peer/host_input/peer_pico",
    "usb-peer-host-storage": TESTS / "sketch_support/usb_peer/host_storage/peer_pico",
    "usb-peer-host-fs": TESTS / "sketch_support/usb_peer/host_storage/peer_pico",
    "usb-peer-host-power": TESTS / "sketch_support/usb_peer/host_hid/peer_pico",
}
# The same cases with another CH32RV board as the peer (the record's usb {to: ch32rv, port, controller}): written and
# talked to through its own address like a DUT (arduino-cli upload, the platform's monitor over SerialDMSeq), staged
# as peer_ch32rv/ with a sketch.yaml for its board. The device cases need it as the host (CH32RVUsbHost), the host
# cases as the device (CH32RVUsbDevice); place.CH32RV_PEER_LACKS says which cases its libraries cannot serve yet.
CH32RV_PEERS = {
    "usb-peer": TESTS / "sketch_support/usb_peer/device/peer_ch32rv",
    "usb-peer-classes": TESTS / "sketch_support/usb_peer/device/peer_ch32rv",
    "usb-peer-fs": TESTS / "sketch_support/usb_peer/device/peer_ch32rv",
    "usb-peer-input": TESTS / "sketch_support/usb_peer/device/peer_ch32rv",
    "usb-peer-firmware": TESTS / "sketch_support/usb_peer/device/peer_ch32rv",
    "usb-peer-host": TESTS / "sketch_support/usb_peer/host_hid/peer_ch32rv",
    "usb-peer-host-classes": TESTS / "sketch_support/usb_peer/host_classes/peer_ch32rv",
    "usb-peer-host-input": TESTS / "sketch_support/usb_peer/host_input/peer_ch32rv",
    "usb-peer-host-storage": TESTS / "sketch_support/usb_peer/host_storage/peer_ch32rv",
    "usb-peer-host-fs": TESTS / "sketch_support/usb_peer/host_storage/peer_ch32rv",
    "usb-peer-host-power": TESTS / "sketch_support/usb_peer/host_hid/peer_ch32rv",
}
# The host peer's FAT (its own, apart from the DUT's FatFS) is the Pico's: staged beside the CH32RV peer's sketch.
PEER_FAT = TESTS / "sketch_support/usb_peer/device/peer_pico/peer_fat.h"
# The Pico peers' TinyUSB: Adafruit TinyUSB Library 3.7.7 (arduino-pico 6.3.x's) with
# sketch_support/usb_peer/pico/adafruit-tinyusb-3.7.7.patch (its RP2040 host sends an OUT data stage as DATA0).
PICO_TINYUSB = dict(version="3.7.7",
                    url="https://github.com/adafruit/Adafruit_TinyUSB_Arduino/archive/refs/tags/3.7.7.tar.gz",
                    sha256="c51a2cf28da568271d385412058e5290bf3afad3f32e6bad8fe5123a3f28bb83",
                    patch=TESTS / "sketch_support/usb_peer/pico/adafruit-tinyusb-3.7.7.patch")
HELPERS = {
    "usb": [TESTS / "instrumented/usb/usb_host.py"],
    "usb-classes": [TESTS / "instrumented/usb/usb_host.py"],
    "usb-media": [TESTS / "instrumented/usb/usb_host.py", TESTS / "instrumented/usb/usb_iso.py"],
    "usb-ss-iso": [TESTS / "instrumented/usb/usb_host.py", TESTS / "instrumented/usb/usb_iso.py"],
    "gpio": [TESTS / "instrumented/bench_lines.py"],
    "uart": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "uart-dma": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "software-serial": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "uart-sync": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "pioc": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "sd": [TESTS / "instrumented/bench_lines.py"],
    "periph": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "signals": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "system": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "spi": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "softspi": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "wire": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "softwire": [TESTS / "instrumented/bench_lines.py", TESTS / "instrumented/ws_capture.py"],
    "usb-peer": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-classes": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-media": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-net": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-fs": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-firmware": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-input": [TESTS / "peer/usb/device/device_peer.py"],
    "usb-peer-host": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-classes": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-input": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-storage": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-fs": [TESTS / "peer/usb/host/host_peer.py"],
    "usb-peer-host-power": [TESTS / "peer/usb/host/host_peer.py"],
}
PLUGINS = ["pytest_embedded.plugin",
           "pytest_embedded_arduino_cli.plugin", "pytest_embedded_arduino_cli_ch32rv.plugin",
           "pytest_embedded_wireskein.plugin", "oep_client.pytest_plugin"]


def invoke(command, log, *, env=None, timeout=180):
    """Run `command` in a process group of its own and take the whole group down when it times out or this runner
    stops (an exception, or SIGTERM / SIGHUP made one by main()): a pytest left behind holds its probe and kept
    running for hours after an outer `timeout` ended the runner (2026-11-18)."""
    log.parent.mkdir(parents=True, exist_ok=True)
    log.with_suffix(".command.json").write_text(json.dumps(command, indent=2))
    with log.open("w") as stream:
        process = subprocess.Popen(command, env=env, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            returncode = process.wait(timeout=timeout)
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
    if returncode:
        raise RuntimeError(f"exit {returncode}: see {log}")
    return log.read_text()


def _stop_on_signal(signum, _frame):
    """SIGTERM / SIGHUP as an exception, so invoke() takes its process group down on the way out."""
    raise SystemExit(128 + signum)


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


def restarts(logs):
    """The DUT's restarts in the middle of a test: (test, reset reason) for each STARTUP line (every sketch prints
    `STARTUP <resetReasonName()>` at boot) that comes after another line of the same test's dut.log. A STARTUP as
    the log's first line is the boot the console caught opening, not a restart."""
    found = []
    for log in sorted(pathlib.Path(logs).glob("**/dut.log")):
        lines = [line.strip() for line in log.read_text(errors="replace").splitlines() if line.strip()]
        for index, line in enumerate(lines):
            if index and line.startswith("STARTUP "):
                found.append((log.parent.name, line.split(None, 1)[1]))
    return found


def pico_tinyusb():
    """The patched Adafruit TinyUSB for the Pico peers, made once per patch in the bench's cache."""
    import tarfile
    import urllib.request
    patch = PICO_TINYUSB["patch"].read_bytes()
    cache = pathlib.Path(os.environ.get("CH32_TEST_ARDUINO_CACHE", pathlib.Path.home() / ".cache/ch32rv-core-bench/arduino"))
    root = cache / "pico-tinyusb" / f"{PICO_TINYUSB['version']}-{hashlib.sha256(patch).hexdigest()[:12]}"
    library = root / "Adafruit_TinyUSB_Arduino"
    if (root / "done").exists():
        return library
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    archive = root / "source.tar.gz"
    with urllib.request.urlopen(PICO_TINYUSB["url"], timeout=60) as response:
        archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != PICO_TINYUSB["sha256"]:
        raise RuntimeError(f"{PICO_TINYUSB['url']}: not the pinned archive (sha256)")
    with tarfile.open(archive) as tar:
        tar.extractall(root, filter="data")
    (root / f"Adafruit_TinyUSB_Arduino-{PICO_TINYUSB['version']}").rename(library)
    subprocess.run(["patch", "-p1", "-d", str(library), "-i", str(PICO_TINYUSB["patch"])], check=True,
                   capture_output=True)
    (root / "done").write_text("")
    return library


def monitor_speed(fqbn):
    """The port_config speed line for the Debug Speed menu's value in the fqbn (written out so the test does not depend
    on the monitor client reading board defaults), else nothing."""
    m = re.search(r"[:,]debugspeed=(\w+)", fqbn)
    return f"      speed: {m.group(1)}\n" if m else ""


def stage_ch32rv_peer(directory, case, target):
    """peer_ch32rv/ beside the case: the peer's sketch, a sketch.yaml for the peer's board (its fqbn and chip, as
    place.peer_place resolved them) and a build_opt.h naming the controller of it the cable reaches - the host port
    for the device cases (the peer is the host), the device controller for the host cases."""
    peer = directory / "peer_ch32rv"
    shutil.copytree(CH32RV_PEERS[case], peer)
    if case in places.PEER_DEVICE_CASES:
        shutil.copyfile(PEER_FAT, peer / PEER_FAT.name)
    fqbn, controller = target["usb_peer_fqbn"], target["usb_peer_ch32rv_controller"]
    (peer / "sketch.yaml").write_text(
        f"profiles:\n  ch32rv:\n    fqbn: {json.dumps(fqbn)}\n"
        f"    port_config:\n      source: dmseq\n      chip: {target['usb_peer_chip']}\n" + monitor_speed(fqbn)
        + "default_profile: ch32rv\n")
    if case in places.PEER_DEVICE_CASES:
        if controller not in USB_HOST_PORTS:
            raise RuntimeError(f"{case}: the CH32RV peer's {controller} has no USB host the peer can run")
        opt = f"-DPEER_USB_HOST_PORT={USB_HOST_PORTS[controller]}\n"
    else:
        opt = f"-DPEER_USB_CONTROLLER=CH32RVUsbController::{USB_CONTROLLERS[controller]}\n"
    (peer / "build_opt.h").write_text(opt)
    return dict(target, usb_peer_port=target["usb_peer_address"])


def stage_case(work, name, case, target):
    directory = work / "cases" / name / case
    directory.mkdir(parents=True)
    module = directory / MODULES[case].name
    shutil.copyfile(MODULES[case], module)
    for helper in HELPERS.get(case, []):
        shutil.copyfile(helper, directory / helper.name)
    if case in PICO_PEERS and target.get("usb_peer") == "pico":
        peer = directory / "peer_pico"
        shutil.copytree(PICO_PEERS[case], peer)
        sketch = peer / "sketch.yaml"
        text = sketch.read_text()
        if "      - TINYUSB\n" not in text:
            raise RuntimeError(f"{PICO_PEERS[case]}/sketch.yaml: no '- TINYUSB' line in its libraries")
        sketch.write_text(text.replace("      - TINYUSB\n", f"      - dir: {json.dumps(str(pico_tinyusb()))}\n"))
        target = dict(target, usb_peer_port=target["usb_peer_address"])
    elif case in CH32RV_PEERS and target.get("usb_peer") == "ch32rv":
        target = stage_ch32rv_peer(directory, case, target)
    elif case in PEERS and target.get("usb_peer") != "ch32rv":
        shutil.copytree(PEERS[case], directory / "peer_s3")
        if target.get("usb_peer") == "p4":
            # The same sketch on the ESP32-P4: its sketch.yaml's p4 profile (the OTG HS port).
            sketch = directory / "peer_s3/sketch.yaml"
            text = sketch.read_text()
            m = re.search(r"^default_profile: s3_(\w+)\n", text, re.M)
            if not m or f"  p4_{m.group(1)}:\n" not in text:
                raise RuntimeError(f"{PEERS[case]}/sketch.yaml: no p4 profile beside the s3 one")
            sketch.write_text(text.replace(m.group(0), f"default_profile: p4_{m.group(1)}\n"))
        target = dict(target, usb_peer_port=places.peer_tty(target["usb_peer_serial"], mac=target.get("usb_peer_mac")))
    opts = BUILD_OPTS.get(case, "")
    # The DUT's controller the record's usb names for this kind of case (per controller: {full_speed: {...}, ...}); the
    # single form leaves the series' usual one (Auto), as before.
    controller = (target.get("usb_host_controller") if case in places.USB_CASES else
                  target.get("usb_peer_controller") if case in places.PEER_DEVICE_CASES + places.PEER_HOST_CASES else None)
    if controller and controller != "auto":
        if case in places.PEER_HOST_CASES:
            if controller not in USB_HOST_PORTS:
                raise RuntimeError(f"{case}: the record's usb.{controller} has no USB host the DUT can run")
            opts += f"-DUSB_PEER_HOST_PORT={USB_HOST_PORTS[controller]}\n"
        else:
            opts += f"-DUSB_TEST_CONTROLLER=CH32RVUsbController::{USB_CONTROLLERS[controller]}\n"
    elif case in places.PEER_HOST_CASES and target.get("usb_peer") == "p4":
        opts += "-DUSB_PEER_HOST_PORT=CH32RV_USB_HOST_PORT_HIGH_SPEED\n"   # the P4 is on the DUT's USBHS
    # Only a sketch on the bench console header can move its console (power has no sketch).
    sketch = SKETCHES.get(case, TESTS / "sketch_support/core_contracts/core_contracts.ino")
    uart = uart_console(case, target) if case != "power" and BENCH_CONSOLE.name in sketch.read_text() else None
    if uart:
        opts += f"-DCONSOLE_UART_RX={uart[0]}\n-DCONSOLE_UART_TX={uart[1]}\n"
    if opts:
        (directory / "build_opt.h").write_text(opts)
    # The probe's lines on the chip's debug pads are never test lines (bench_lines.gpio_lines).
    target = dict(target, debug_pads=places.debug_pads(target["chip"]))
    # A console on the probe's UART lines takes their pads: no test drives them (bench_lines.port_lines). The boards
    # label them plain too (<port>@PB14 beside <port>.rx): H417's complementary took PB14 / PB15 as TIM1's CH2N / CH3N
    # and the console stopped in the middle of a line (2026-11-21).
    if uart:
        target = dict(target, console_pads=list(uart))
    if case in ("selfcheck", "selfbus", "selfsys", "selfapi"):
        target = dict(target, gpio_keep_off=places.gpio_keep_off(target["chip"]))
    if case == "selfsys":
        target = dict(target, selfsys_pins=places.gpio_pads(target["chip"]))
    if case in ("eth", "eth-libs"):
        clocked = places.ethernet_clock(target["fqbn"])
        if clocked:
            target = dict(target, fqbn=clocked[0], f_cpu=clocked[1])
    build_id = uuid.uuid4().hex[:12]
    staged = {k: v for k, v in target.items() if k != "snapshot"}
    settings = dict(build_id=build_id, target=staged, f_cpu=target["f_cpu"], case=case)
    # The lines of gpio, spi, softspi, wire, softwire and sd come from the probe's labels (bench_lines.py).
    (directory / "case.json").write_text(json.dumps(settings, indent=2))
    if case != "power":
        shutil.copyfile(sketch, directory / f"{case}.ino")
        # Its assembly beside it (a PIOC program, sketch_support/pioc): arduino-cli builds a sketch's .S files.
        for extra in sorted(sketch.parent.glob("*.S")):
            shutil.copyfile(extra, directory / extra.name)
        shutil.copyfile(BENCH_CONSOLE, directory / BENCH_CONSOLE.name)
        # JSON strings are valid YAML scalars. No platform version is requested: use the isolated worktree platform.
        (directory / "sketch.yaml").write_text(
            f"profiles:\n  {name}:\n    fqbn: {json.dumps(target['fqbn'])}\n"
            + "".join(f"{'    libraries:' + chr(10) if i == 0 else ''}      - {lib}\n"
                      for i, lib in enumerate(CASE_LIBRARIES.get(case, ())))
            + ("    protocol: ch32rv-hid\n" if case == "hid" or target.get("upload_port", "").startswith("ch32rv-hid://") else "")
            +
            f"    port_config:\n      source: {'fixture-uart' if uart else CONSOLE_SOURCES.get(case, 'dmseq')}\n"
            + ("      baudrate: 115200\n" if uart else "")
            +
            f"      chip: {target['chip']}\n"
            # The Debug Speed menu's value for the monitor too.
            + monitor_speed(target["fqbn"])
        )
        flag = CONSOLE_FLAGS.get(case)
        (directory / "build_config.toml").write_text('[defines]\nCH32_TEST_BUILD_ID = "BENCH_BUILD_ID"\n'
                                                     + (f"[flags]\n{flag} = true\n" if flag else ""))
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
                # With its device number: a bootloader left on the bus re-enumerates under the same name.
                out.add((dev.name, (dev / "devnum").read_text().strip()))
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
        # PREPARE_BOOT right after the NRST pulse (oep-client-python 0.0.60): with the 0.3 s it waited before, a sketch
        # of this core that reads resetReason() in setup() had cleared PINRSTF first and the bootloader left at once.
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
        usb = pathlib.Path("/sys/bus/usb/devices") / new.pop()[0] if new else None
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
    try:
        link_records = places.link_records_from_env()
    except places.PlaceError as error:
        parser.error(str(error))
    tool, toolchain, versions = tool_paths()
    work = args.out.resolve()
    work.mkdir(parents=True, exist_ok=False)
    env = isolated_environment(work, tool, toolchain)
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
                                        chip=chip, record=link_records.get(places.canonical(address)))
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
                module = None
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
                    command = [sys.executable, "-m", "pytest", "-c", str(TESTS / "pyproject.toml")]
                    for plugin in PLUGINS:
                        command += ["-p", plugin]
                    command += [str(module), "--profile", name, "--port", staged["port"],
                                "--run-mode", "all" if args.execute else "build",
                                "--device-lock", "required", "--root-logdir", str(module.parent / "logs"),
                                "--junitxml", str(module.parent / "junit.xml"), "-o", "junit_family=xunit1", "-s", "-q"]
                    if case in PEER_CASES and staged.get("usb_peer") == "pico":
                        case_env["TEST_SERIAL_PORT_PEER_PICO"] = staged["usb_peer_port"]
                    elif case in PEER_CASES and staged.get("usb_peer") == "ch32rv":
                        case_env["TEST_SERIAL_PORT_PEER_CH32RV"] = staged["usb_peer_port"]
                    elif case in PEER_CASES:
                        command += ["--peer-port", "s3:" + staged["usb_peer_port"]]
                    if case == "hid":
                        command += ["--flash-port", "ch32rv-hid://" + staged["soft_usb_topology"]]
                    elif staged.get("upload_port"):
                        command += ["--flash-port", staged["upload_port"]]
                    # A peer case builds the ESP32 sketch inside pytest too; on a loaded
                    # host that alone can take minutes. signals streams a capture per test (two dozen of them,
                    # 204 s on X035 with the probe busy, 2026-11-19).
                    invoke(command, module.parent / "pytest.log", env=case_env,
                           timeout=900 if case in PEER_CASES else 600 if case == "signals" else 300)
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
                    if args.execute and module is not None:
                        # A test that failed because the DUT restarted under it says so first.
                        restarted = restarts(module.parent / "logs")
                        if restarted:
                            result["restarted"] = [f"{test}: {reason}" for test, reason in restarted]
                            result["error"] = ("the DUT restarted (" + "; ".join(result["restarted"]) + "); "
                                               + result["error"])
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
    signal.signal(signal.SIGTERM, _stop_on_signal)
    signal.signal(signal.SIGHUP, _stop_on_signal)
    raise SystemExit(main())
