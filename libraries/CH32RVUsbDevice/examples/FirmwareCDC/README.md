# FirmwareCDC

[日本語](README.ja.md)

Receives a new sketch over a USB serial port (interface name `Firmware`), checks it and installs it (the same steps as
EspUsbDevice's FirmwareCDC). The PC side is [firmware_cdc.py](firmware_cdc.py).

```
host "FW <size>\n" -> device "READY\n" -> <size> bytes -> device "OK\n" -> restart into it
```

## What you need and the wiring

A part with a USB device port, and a USB cable to the PC. Progress on a 115200 bps UART monitor (`RX` / `TX`). Python and
pyserial on the PC.

## What to do and what to expect

1. After the upload the UART shows `Ready: up to <bytes> bytes ...`: the largest image it takes (the flash less this
   sketch; about 13 KB on a 32 KB V203).
2. Send the new sketch's `.bin` (Arduino's "Export compiled binary"):
   `python3 firmware_cdc.py /dev/ttyACM0 Blink.ino.bin`
3. After `accepted ... bytes` the board starts the new sketch.
4. Too big: `device refused: ERR Not Enough Space` before anything is sent. Not a sketch: `ERR Wrong Magic Byte`, and this
   sketch stays.

## The API and the flow

| API | Its use here |
|---|---|
| `Update.begin(size)` | The size first: an image that does not fit is refused before the host sends it. |
| `Update.write(buffer, n)` | In arrival order. The flash erase runs in `loop()`, and the host waits meanwhile. |
| `Update.end()` | Checks the image and arms the swap at the restart. |
| `CH32RV.restart()` | Installs it and starts it. |

Other ways in: DFU is [FirmwareDFU](../FirmwareDFU/README.md), a copy to a drive is [FirmwareMSC](../FirmwareMSC/README.md),
the UART is the Update library's SerialUpdate. Power lost during the swap (the copy to the start) leaves the sketch
half written (a probe recovers it).
