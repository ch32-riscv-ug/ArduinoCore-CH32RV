# FirmwareBootMode

[日本語](README.ja.md)

Enters the chip's factory USB bootloader from the running sketch (EspUsbDevice's FirmwareBootMode), so the whole flash
can be written again with no one touching BOOT. The bootloader is in the chip's own boot area, so this works for a
sketch with no update code of its own, and is the recovery route behind every other update.

## What you need and the wiring

A CH32X035 / X033 (where `CH32RVUsbDevice::enterBootloader()` reaches the factory bootloader, 1a86:55e0; X305 / X315 have
the same registers but are not tried; on the other series it is a plain reset, their bootloader taking the BOOT0 pin
only) and a USB cable to the PC. Logs on a 115200 bps UART (`RX` / `TX`). ch32rv (the core's upload tool) on the PC.

## What to do and what to expect

The sketch shows a CDC port named `Console` and a DFU runtime interface. Three ways reach the bootloader:

| Gesture | Host side |
|---|---|
| Open the CDC port at 1200 baud and drop DTR | `arduino-cli upload ...`, the Arduino IDE, or `stty -F /dev/ttyACM0 1200` |
| `DFU_DETACH` | `dfu-util -e` (`-d 1209:0001` among several devices) |
| Send `b` on the Console port | `printf b > /dev/ttyACM0` |

The device leaves the bus and comes back as 1a86:55e0; then write it as usual (`ch32rv isp`, or arduino-cli upload with
the USB route). The first two need no code: the CDC port and the DFU runtime interface handle them. The third is
`CH32RVUsbDevice::enterBootloader()`, which does not return (inside a USB callback use `requestBootloader()`, which
waits until the answer has gone out).
