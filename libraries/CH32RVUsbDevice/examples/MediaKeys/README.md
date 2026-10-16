# MediaKeys

日本語: [README.ja.md](README.ja.md)

Consumer media-key reports plus a declared System Control function.

## Hardware and connections

A PC with volume/media controls and a UART monitor. Volume and mute change automatically after connection.

## Steps and expected results

1. Volume up/down, mute, play/pause, next and previous cycle about every three seconds.
2. Compare UART `MEDIA_KEY` messages with host behavior.
3. Use a HID viewer if a player ignores a particular usage.

## Settings and limits

Power-off, standby and wake calls are commented out and are not sent by this sketch. Enable them only as part of an explicit input action in your application.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MediaKeys.ino](MediaKeys.ino)
