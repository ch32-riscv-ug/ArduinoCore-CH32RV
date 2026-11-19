# FirmwareVendor

[日本語](README.ja.md)

A firmware update over a vendor interface (the same protocol as EspUsbDevice's FirmwareVendor): control requests carry
the commands, bulk OUT carries the image, and the Update library checks it before installing it. The PC side is
[firmware_vendor.py](firmware_vendor.py). The fastest of the USB update routes (bulk, not EP0); the cost is a host tool
of your own. [FirmwareDFU](../FirmwareDFU/README.md) does the same job with a standard tool.

## What you need and the wiring

A part with a USB device port and a USB cable to the PC. Progress on a 115200 bps UART monitor (`RX` / `TX`). Python and
pyusb on the PC (on Linux, write access to the device node: a udev rule for 1209:0001, or sudo).

## Protocol

| Direction | Request | Meaning |
|---|---|---|
| control OUT `0x40` | `0x01`, `wValue` = size bits 0-15, `wIndex` = bits 16-31 | start (`Update.begin(size)`) |
| bulk OUT | - | the image |
| control OUT `0x40` | `0x02` | commit: `Update.end()` checks it, then the device restarts into it; a stall when the check fails |
| control OUT `0x40` | `0x03` | abort |
| control IN `0xC0` | `0x04` -> 8 bytes | `[0]` receiving, `[1..3]` the Update library's error, `[4..7]` bytes written (LE) |

Commands on EP0 and data on bulk: the status request is answered at any time, also while bulk OUT is busy, so the host
can watch the progress. Any other vendor request is stalled.

## What to do and what to expect

1. After the upload the UART shows `Ready: up to <bytes> bytes ...`: the largest image it takes.
2. Send the new sketch's `.bin` (Arduino's "Export compiled binary"): `uv run --with pyusb python3 firmware_vendor.py Blink.ino.bin`
3. After `accepted` the board starts the new sketch.
4. Too big: `device refused the image` before anything is sent (Update error 4, no space). Not a sketch for this chip:
   `device stopped at 256 bytes` at the first block (Update error 8, the wrong magic byte). This sketch stays either way.
   A check that fails only at the end gives `device refused to install it` at the commit.

The rest of the image waits in the device's 2 KB FIFO while a flash page is erased (the host is held with NAKs), so
nothing is lost. Power lost during the swap (the copy to the start) leaves the sketch half written (a probe recovers it).

Checked on CH32H417 over SuperSpeed (2026-11-20): a 32 KB sketch (VendorBulkPeer) written in 0.14 s and started; a
file that is not a sketch stopped at the first block (error 8) and one too big refused at START (error 4), FirmwareVendor
staying.
