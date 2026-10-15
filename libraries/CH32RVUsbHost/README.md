# CH32RVUsbHost — the chip's own USB as a host

Keyboards, mice and other HID, USB serial (CDC-ACM and FTDI / CP210x / CH34x /
PL2303), MIDI, mass storage, vendor bulk interfaces and CCID smart-card
readers, through a hub too. The API is EspUsbHost's (the same author's ESP32
library), renamed EspUsbHost -> CH32RVUsbHost.

Runs on CH32X035 R8T6 / C8T6 / G8U6 / G8R6 (the F8U6, F7P6, D8U6 and the X033
have no host in silicon; `begin()` says Unsupported), CH32V203/V208 (PB6/PB7)
and CH32V303/V305/V307/V317 (PA11/PA12). Verified on a CH32X035C8T6 against an
ESP32-S3 (HID, CDC, MSC, MIDI, vendor, CCID); V20x / V30x are build-only. The board
supplies VBUS. Not here: isochronous audio / video and USB networking.

The host controller driver (`ch32rv_hcd_usbfs.c` in CH32RVTinyUSB) is this
core's own: every open endpoint can have a transfer pending, NAKs are retried
at the next frame (interrupt endpoints at their interval), STALL / no response
/ toggle errors are told apart, control transfers give up after 5 s. See
README.ja.md for the full description, and tests/peer/usb/host for the peer
tests against an ESP32-S3.
