# Keyboard

日本語: [README.ja.md](README.ja.md)

A USB keyboard sending text, JIS symbols and a raw HID usage, while receiving Lock LED output reports.

## Hardware and connections

A PC text editor and UART monitor. Focus the editor before connecting: the sketch generates input automatically about every three seconds.

## Steps and expected results

1. Check UART for `USB keyboard ready`.
2. Observe `Hi from CH32RVUsbDevice` and the following symbol input in the editor.
3. Toggle Caps Lock to see `LEDS num=... caps=...` on UART.
4. The sketch switches its conversion table US→JIS→US and also sends the LANG1 usage.

## Settings and limits

`setLayout()` chooses character-to-usage conversion; it does not change the PC keyboard layout. Match the PC layout when checking symbols. There is one `onOutputReport()` callback slot, and `ledState()` also exposes the latest state.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
