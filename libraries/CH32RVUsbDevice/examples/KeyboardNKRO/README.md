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

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[KeyboardNKRO.ino](KeyboardNKRO.ino)
