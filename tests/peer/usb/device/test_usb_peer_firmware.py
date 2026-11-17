"""usb.peer-dfu-download, usb.peer-dfu-refuse, usb.peer-uf2-drive, usb.peer-uf2-refuse, usb.peer-raw-drive.

The DUT runs tests/sketch_support/usb_firmware: CH32RVUsbDeviceDfu (DFU mode, protocol 2) and
CH32RVUsbDeviceMscFirmwareDisk beside the default DFU runtime. The peer - the USB host - gets the DUT's own image (the
.bin this case built) with the 16 hex digits after "CH32GEN:" changed, in hex lines (fwclr / fwput), and hands it to
the DUT by DFU 1.1 (dfuall, or dfudn a block for the refusals) or as sectors on its drive (uf2w builds the UF2
blocks itself, rawall writes the .bin; one command each, the peer's console being slow); the new digits can
only come back from an image that was written, checked, installed and started. The refusals send what must not be
installed (bytes that are not an image, a download abandoned half way, a UF2 for another family) and check the old
digits are still there. A peer, not the PC: these are the update protocols, not a class driver's transfers.

The raw .bin drive ends with an eject (START STOP UNIT, LoEj): the Pico peer (TinyUSB) sends one, EspUsbHost has no
public SCSI command for it - that part is left out there (recorded). The UF2 refusal comes last: its stalled write
leaves the peer's drive unusable (no reset recovery in either host stack).

Writes the DUT's program flash several times: the runner builds it with CH32RV_BOOT_HOLD_MS (the case's notes).
The image must fit the staging area the DUT reports (about the flash less the sketch), and the peer's buffer (96 KiB).
"""
import pathlib
import re
import secrets
import sys
import time

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from device_peer import Peer, command, dut_ready, settings, wait_enumerated

TAG = b"CH32GEN:"
DFU_IDLE, DFU_DNLOAD_IDLE, DFU_MANIFEST, DFU_ERROR = 2, 5, 7, 10
ERR_WRITE = 3
UF2_FAMILY_ERROR = 30
PEER_BUFFER = 96 * 1024
CHUNK = 512                        # bytes a fwput line: 1024 hex digits, under the peers' 1200-character lines


def fw(dut, timeout=15):
    """FW from the DUT, asked again until it answers (the console comes back after a restart)."""
    deadline = time.monotonic() + timeout
    while True:
        dut.write("fw\n")
        try:
            m = dut.expect(rb"FW ([0-9a-f]{16}) (\d+) ([0-9A-F]+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)\r?\n",
                           timeout=2)
            g = m.groups()
            return {"generation": g[0].decode(), "capacity": int(g[1]), "family": int(g[2], 16),
                    "ram_sectors": int(g[3]), "blocks": int(g[4]), "dfu_status": int(g[5]), "dfu_state": int(g[6]),
                    "disk_error": int(g[7]), "dfu_errors": int(g[8]), "disk_written": int(g[9])}
        except Exception:
            if time.monotonic() >= deadline:
                raise


def with_generation(image, generation):
    at = image.index(TAG) + len(TAG)
    return image[:at] + generation.encode() + image[at + 16:]


def load(peer, data):
    """The peer's buffer holds `data` (fwclr, then fwput lines)."""
    command(peer, "fwclr", rb"FWCLR")
    for at in range(0, len(data), CHUNK):
        m = command(peer, f"fwput {at} {data[at:at + CHUNK].hex()}", rb"FWPUT (\d) (\d+)")
        assert m.group(1) == b"1", f"the peer did not take the bytes at {at}"
    assert int(m.group(2)) == len(data)


def set_generation(peer, image):
    """A new generation in the peer's buffer: only its 16 digits are sent again (the Pico peer's console takes about
    100 bytes a second), the rest of the image stays where load() put it. Returns the generation."""
    generation = secrets.token_hex(8)
    at = image.index(TAG) + len(TAG)
    m = command(peer, f"fwput {at} {generation.encode().hex()}", rb"FWPUT (\d) (\d+)")
    assert m.group(1) == b"1", "the peer did not take the generation"
    return generation


def dfu_block(peer, number, at, length):
    m = command(peer, f"dfudn {number} {at} {length}", rb"DFUDN (\d) (\d+) (\d+)", timeout=20)
    assert m.group(1) == b"1", f"DNLOAD {number} or its GETSTATUS failed on the peer"
    return int(m.group(2)), int(m.group(3))


def dfu_simple(peer, what):
    m = command(peer, f"dfu{what}", rb"DFU" + what.upper().encode() + rb" (\d) (\d+) (\d+)", timeout=10)
    assert m.group(1) == b"1", f"DFU {what} failed on the peer"
    return int(m.group(2)), int(m.group(3))


def come_back(peer, dut, build_id, timeout=30):
    """The DUT leaves the bus to install the image and enumerates again (wait_enumerated), then FW."""
    deadline = time.monotonic() + 10
    while True:
        peer.write("dev\n")
        text = peer.serial.expect(rb"(?:[^\n]*\n)*?" + re.escape(peer.tag) + rb"DEVEND (\d+)\r?\n", timeout=10)
        if build_id.encode() not in text.group(0):
            break
        assert time.monotonic() < deadline, "the device did not leave the bus to install the image"
        time.sleep(0.2)
    wait_enumerated(peer, build_id, timeout=timeout)
    return fw(dut)


def test_usb_peer_firmware(dut, peers, record_property):
    peer = Peer(peers)
    here = pathlib.Path(__file__).parent
    build_id = settings(__file__)["build_id"]
    dut_ready(dut, build_id)
    wait_enumerated(peer, build_id)

    image = next(here.glob("build/*/*.ino.bin")).read_bytes()
    first = fw(dut)
    record_property("image_bytes", len(image))
    record_property("staging_bytes", first["capacity"])
    if len(image) > first["capacity"]:
        pytest.skip(f"the image ({len(image)} B) does not fit the staging area ({first['capacity']} B) on this part")
    if len(image) + 512 > PEER_BUFFER:
        pytest.skip(f"the image ({len(image)} B) does not fit the peer's buffer ({PEER_BUFFER} B)")
    assert image.count(TAG) == 1
    m = command(peer, "dfuinfo", rb"DFUINFO (-?\d+) (\d+)")
    itf, transfer = int(m.group(1)), int(m.group(2))
    assert itf >= 0 and transfer > 0, f"no DFU-mode interface with a transfer size seen by the peer ({itf}, {transfer})"
    record_property("dfu_transfer_size", transfer)

    # usb.peer-dfu-refuse: bytes that are not an image fail on their first block (errWRITE, dfuERROR), and a download
    # abandoned half way (DFU_ABORT) installs nothing; CLRSTATUS / ABORT bring the interface back to dfuIDLE.
    load(peer, secrets.token_bytes(transfer))
    assert dfu_block(peer, 0, 0, transfer) == (ERR_WRITE, DFU_ERROR), "a block that is no image was taken"
    dfu_simple(peer, "clr")
    assert dfu_simple(peer, "st") == (0, DFU_IDLE)
    # The image is sent once (the Pico peer's console is slow); every later step changes only its generation.
    load(peer, with_generation(image, secrets.token_hex(8)))
    half = max(transfer, len(image) // 2)
    for n, at in enumerate(range(0, half, transfer)):
        assert dfu_block(peer, n, at, min(transfer, half - at)) == (0, DFU_DNLOAD_IDLE)
    dfu_simple(peer, "abort")
    assert dfu_simple(peer, "st") == (0, DFU_IDLE)
    time.sleep(1)
    assert fw(dut)["generation"] == first["generation"], "a refused download changed the running image"

    # usb.peer-dfu-download: the image with a new generation, installed and running after the restart.
    generation = set_generation(peer, image)
    started = time.monotonic()
    m = command(peer, "dfuall", rb"DFUALL (\d) (\d+) (\d+) (\d+)", timeout=120)
    status, state = int(m.group(3)), int(m.group(4))
    assert m.group(1) == b"1" and (status, state) == (0, DFU_MANIFEST), \
        f"after {m.group(2).decode()} blocks: status {status} state {state}"
    after = come_back(peer, dut, build_id)
    record_property("dfu_download_s", round(time.monotonic() - started, 3))
    assert after["generation"] == generation, "the downloaded image is not the one running"

    # usb.peer-uf2-drive: every block, last first (UF2's blocks say where they go), then the restart.
    generation = set_generation(peer, image)
    started = time.monotonic()
    m = command(peer, f"uf2w {after['ram_sectors']} {after['family']:x} 0 1", rb"UF2W (\d) (\d+) (\d+)", timeout=120)
    written, total = int(m.group(2)), int(m.group(3))
    assert written >= total - 1, f"UF2: {written} of {total} blocks taken (the last may meet the device leaving)"
    after = come_back(peer, dut, build_id)
    record_property("uf2_s", round(time.monotonic() - started, 3))
    assert after["generation"] == generation, "the UF2 image is not the one running"

    # usb.peer-raw-drive: the .bin in order, ended by the host's eject (START STOP UNIT, LoEj) - the Pico peer only.
    if peer.kind == "pico":
        generation = set_generation(peer, image)
        pad = -len(image) % 512
        if pad:
            m = command(peer, f"fwput {len(image)} {'ff' * pad}", rb"FWPUT (\d) (\d+)")
            assert m.group(1) == b"1" and int(m.group(2)) == len(image) + pad
        m = command(peer, f"rawall {after['ram_sectors']}", rb"RAWALL (\d)", timeout=120)
        assert m.group(1) == b"1", "the raw sectors were refused"
        m = command(peer, "mscej", rb"MSCEJ (\d) (\d+)", timeout=20)
        assert m.group(1) == b"1", "the eject failed"
        after = come_back(peer, dut, build_id)
        assert after["generation"] == generation, "the raw image is not the one running"
    else:
        record_property("raw_drive", f"not run: the {peer.kind} peer has no SCSI eject")

    # usb.peer-uf2-refuse, last: a UF2 block for another family is refused at once and nothing is installed. The
    # refusal stalls the write, which neither peer's host recovers from (TinyUSB's MSC host does no reset recovery),
    # so nothing is asked of the drive after it.
    set_generation(peer, image)
    m = command(peer, f"uf2w {after['ram_sectors']} {after['family'] ^ 0x5A5A5A5A:x} 1 0", rb"UF2W (\d) (\d+) (\d+)",
                timeout=20)
    assert m.group(1) == b"0", "a UF2 block for another family was accepted"
    state = fw(dut)
    assert state["disk_error"] == UF2_FAMILY_ERROR and state["generation"] == after["generation"]
