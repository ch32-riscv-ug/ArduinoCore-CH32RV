# MscCdcBlocks

[日本語](README.ja.md)

A composite device: a raw 8 KB block device (no file system, lost at reset) and beside it a serial port that reports how
many times the host read and wrote it (as EspUsbDevice's MscCdcBlocks).

## What you need and the wiring

A part with a USB device port, and a USB cable to the PC. Failures show on a 115200 bps UART monitor (`RX` / `TX`).

## What to do and what to expect

The PC sees an 8 KB drive (`CH32RV` / `RAM blocks`) and a serial port (interface name `disk statistics`). The drive is
all zeros, and the OS offers to format it. With the serial port open, `reads=... writes=...` shows every second, and at
once on `?`. The counts grow as the PC reads and writes the drive.

## The API and the flow

| API | Its use here |
|---|---|
| `disk.onRead()` / `onWrite()` | Block reads and writes, called while the host waits for the answer (inside USB processing): keep them short. Out of range: -1. |
| `disk.begin(blocks, size)` | The block count and size. |
| `disk.vendorID()` / `productID()` / `mediaPresent(true)` | The names the host sees, and a medium present. |
| `CH32RVUsbDeviceCdcSerial console(device, "disk statistics")` | A named serial port. The counts are calls of the block functions, not bytes. |

For files use [MSCFatRamDisk](../MSCFatRamDisk/README.md); to keep the data, put a store such as an SD card behind
`onRead()` / `onWrite()`.
