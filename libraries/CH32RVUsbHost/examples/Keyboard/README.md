# Keyboard

日本語: [README.ja.md](README.ja.md)

USB keyboard ASCII input printed on UART, with Caps/Num Lock LED updates.

## Hardware and connections

A USB keyboard, powered host wiring and a 115200-baud UART monitor.

## Steps and expected results

1. Type ASCII characters and observe UART; Enter prints a newline.
2. Toggle Caps Lock and Num Lock to check keyboard LEDs.
3. For JIS, enable the commented JA_JP layout selection, rebuild and check symbols.

## Settings and limits

Only press events with ASCII are printed. Releases, modifiers alone and IME composition are not a text-console function. setKeyboardLayout chooses host-side character decoding; it does not program the physical keyboard.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
