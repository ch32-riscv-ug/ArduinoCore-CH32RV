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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `setLayout(...)` | Selects ASCII-to-HID conversion. Match the PC keyboard layout as well. |
| `write("...")` / `tapKey('\n')` | Send text or one character as press/release operations. This is not a UTF-8 Japanese text input API. |
| `tapUsage(...)` | Sends a raw HID key usage without ASCII conversion. Specify a key identifier, not a character code. |
| `onOutputReport()` / `ledState()` | Receive or inspect host Lock LED state. Notifications require `deliverEvents()`. |

Changing the device layout changes character conversion, not the PC layout or IME mode. This sample temporarily uses JIS conversion; check PC layout and IME state if symbols differ from those expected.

For physical buttons, call `pressUsage()` on press and `releaseUsage()` or `releaseAll()` on release. Reporting only presses leaves the host believing keys are held. Consider retrying a release after transmission failure.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Keyboard.ino](Keyboard.ino)
