"""USB DFU 1.1 download to an OEP probe, in pyusb: what `dfu-util -D` does, without needing dfu-util on the bench.

The probe (oep-probe-arduino >= 0.0.16 on the ESP32-P4) carries a DFU interface in download mode next to its
vendor / HID / CDC ones; the running firmware takes the image into the other app slot, verifies it and reboots
into it (rollback-protected). The device is gone for a moment after the manifest - that is the reboot, and on a
WSL bench usbipd has to attach it again (prepare.reattach_usbip).

    uv run tests/bench/dfu.py <image.bin> --serial <probe serial>          # also usable by hand
"""
from __future__ import annotations

import struct
import sys
import time

VID, PID = 0x303A, 0x0002
DFU_DETACH, DFU_DNLOAD, DFU_UPLOAD, DFU_GETSTATUS, DFU_CLRSTATUS, DFU_GETSTATE, DFU_ABORT = range(7)
STATES = ["appIDLE", "appDETACH", "dfuIDLE", "dfuDNLOAD-SYNC", "dfuDNBUSY", "dfuDNLOAD-IDLE", "dfuMANIFEST-SYNC",
          "dfuMANIFEST", "dfuMANIFEST-WAIT-RESET", "dfuUPLOAD-IDLE", "dfuERROR"]
STATUS = ["OK", "errTARGET", "errFILE", "errWRITE", "errERASE", "errCHECK_ERASED", "errPROG", "errVERIFY",
          "errADDRESS", "errNOTDONE", "errFIRMWARE", "errVENDOR", "errUSBR", "errPOR", "errUNKNOWN", "errSTALLEDPKT"]
DNLOAD_IDLE, MANIFEST_WAIT_RESET, IDLE, ERROR = 5, 8, 2, 10


class DfuError(Exception):
    pass


def find(serial: str | None):
    """The probe's USB device (by serial when given), or None."""
    import usb.core
    import usb.util
    for d in usb.core.find(find_all=True, idVendor=VID, idProduct=PID):
        try:
            sn = usb.util.get_string(d, d.iSerialNumber)
        except Exception:      # noqa: BLE001 - a device we cannot read is not ours
            continue
        if serial is None or sn == serial:
            return d
    return None


def dfu_interface(dev):
    """(interface, wTransferSize) of the device's DFU interface, from its functional descriptor."""
    for intf in dev.get_active_configuration():
        if intf.bInterfaceClass == 0xFE and intf.bInterfaceSubClass == 1:
            extra = bytes(intf.extra_descriptors)
            i = 0
            while i + 1 < len(extra):
                ln, ty = extra[i], extra[i + 1]
                if ty == 0x21 and ln >= 9:          # DFU functional descriptor
                    _attrs, _detach, xfer, _ver = struct.unpack_from("<BHHH", extra, i + 2)
                    return intf, xfer
                i += max(ln, 1)
            return intf, 4096
    raise DfuError("the probe has no DFU interface (class FE/01): its firmware predates 0.0.16")


def download(serial: str | None, image: bytes, log=print) -> float:
    """Send `image`; -> seconds taken. Raises DfuError with the DFU status word when the probe refuses (errVERIFY
    for an image that is not this probe's: the running firmware stays)."""
    import usb.core
    import usb.util
    dev = find(serial)
    if dev is None:
        raise DfuError(f"no OEP probe {VID:04x}:{PID:04x} with serial {serial!r}")
    intf, xfer = dfu_interface(dev)
    n = intf.bInterfaceNumber
    try:
        usb.util.claim_interface(dev, n)
    except usb.core.USBError:
        pass                                    # a kernel that holds it still lets EP0 through

    def status():
        r = bytes(dev.ctrl_transfer(0xA1, DFU_GETSTATUS, 0, n, 6, 5000))
        return r[0], r[1] | (r[2] << 8) | (r[3] << 16), r[4]

    st, _, state = status()
    if state == ERROR:
        dev.ctrl_transfer(0x21, DFU_CLRSTATUS, 0, n, b"", 5000)
    log(f"dfu: interface {n}, {xfer}-byte blocks, {len(image)} bytes")
    t0 = time.monotonic()
    block = 0
    for off in range(0, len(image), xfer):
        dev.ctrl_transfer(0x21, DFU_DNLOAD, block, n, image[off:off + xfer], 5000)
        while True:
            st, poll, state = status()
            if st:
                raise DfuError(f"block {block}: {STATUS[st]} ({STATES[state]})")
            if state == DNLOAD_IDLE:
                break
            time.sleep(poll / 1000)
        block += 1
    dev.ctrl_transfer(0x21, DFU_DNLOAD, block, n, b"", 5000)          # the end of the image: manifest
    for _ in range(100):
        try:
            st, poll, state = status()
        except usb.core.USBError:
            break                               # rebooting into the new image (manifestation intolerant)
        if st:
            raise DfuError(f"manifest: {STATUS[st]} - the running firmware stays")
        if state in (IDLE, MANIFEST_WAIT_RESET):
            break
        time.sleep(max(poll, 50) / 1000)
    dt = time.monotonic() - t0
    log(f"dfu: {len(image)} bytes in {block} blocks, {dt:.1f} s")
    return dt


def wait_reboot(serial: str | None, log=print, gone_s: float = 10.0, back_s: float = 30.0) -> None:
    """Wait for the device to disappear (the reboot) and show up again."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < gone_s and find(serial) is not None:
        time.sleep(0.2)
    t0 = time.monotonic()
    while time.monotonic() - t0 < back_s:
        if find(serial) is not None:
            log("dfu: the probe is back")
            return
        time.sleep(0.5)
    log("dfu: the probe has not re-enumerated yet (a WSL bench needs usbipd to attach it again)")


def main() -> int:
    import argparse
    import pathlib
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", type=pathlib.Path)
    ap.add_argument("--serial", default=None)
    a = ap.parse_args()
    try:
        download(a.serial, a.image.read_bytes())
        wait_reboot(a.serial)
    except DfuError as e:
        print(f"dfu: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
