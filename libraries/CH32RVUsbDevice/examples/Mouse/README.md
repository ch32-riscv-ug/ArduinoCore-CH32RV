# Mouse

日本語: [README.ja.md](README.ja.md)

A USB mouse generating relative movement, a wheel event and a left-button press/release without external sensors.

## Hardware and connections

A PC and UART monitor. The sketch moves and clicks automatically, so use a test screen.

## Steps and expected results

1. Movement steps right 30, down 30, left 30, up 30 occur about one second apart.
2. A +1 wheel step follows, then a roughly 50 ms left-button press/release.
3. The six steps repeat; UART reports transmission failures as `MOUSE_FAILED`.

## Settings and limits

`move()` takes relative movement, not absolute screen coordinates. Pointer acceleration affects visible travel. When replacing the sequence with buttons, send both press and release states.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Mouse.ino](Mouse.ino)
