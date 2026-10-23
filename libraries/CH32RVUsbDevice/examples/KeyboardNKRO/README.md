# KeyboardNKRO

日本語: [README.ja.md](README.ja.md)

An NKRO bitmap report holding ten keys plus Left Shift, with six-key reports retained for boot protocol.

## Hardware and connections

A PC HID report/key-state viewer and UART monitor. Use a test input window because input is generated automatically.

## Steps and expected results

1. Keep `enableNkro()` before `device.begin()`.
2. About every four seconds, A/S/D/F/G/H/J/K/L/semicolon plus Left Shift are held for about 200 ms.
3. After `releaseAll()`, UART should show `A still down? no`.
4. Use report or key-state inspection; text output alone does not demonstrate simultaneous keys.

## Settings and limits

`sendReport()` sends the whole held state in one report, unlike repeated `pressUsage()` calls. The host selects boot/report protocol; boot reports cannot represent all NKRO keys.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `enableNkro()` | Call before `device.begin()` to advertise bitmap key reports. |
| `CH32RVUsbDeviceNkroKeyboardReport::press(usage)` | Adds a key to a report before sending; modifier usages are accepted too. |
| `keyboard.sendReport(report)` | Sends the complete key set as one report. Check the boolean result. |
| `releaseAll()` / `heldState()` | Send release of every key and inspect the library's retained reported state. |
| `protocol()` | Reports the host-selected boot/report protocol, separately from the NKRO setting. |

Repeated `pressUsage()` calls send intermediate states. Build a report and send it once to present a chord together. Boot protocol cannot represent the same number of held keys, so inspect the host's selected protocol as well.

`heldState()` does not read physical switches. Add scanning and debounce for a real keyboard, then send changed state. The sample does not log every transmission result.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[KeyboardNKRO.ino](KeyboardNKRO.ino)
