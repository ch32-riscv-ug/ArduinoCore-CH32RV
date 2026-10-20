# SerialMulti

日本語: [README.ja.md](README.ja.md)

Two independent CDC ports, Console and Data Link, on one USB device.

## Hardware and connections

Two PC serial terminals, and optionally a UART monitor.

## Steps and expected results

1. Build and upload the sketch.
2. Open both CDC ports on the PC.
3. Characters return with `CONSOLE: ...` or `DATALINK: ...`, depending on the port.
4. Each open port receives its own tick about every three seconds; UART logs its DTR changes.

## Settings and limits

A device takes up to four CDC ports; a fifth reports `TooManyInstances`. The ESP32-P4 conditional block is inactive on CH32, so there is no Telemetry port. Two CDC functions use four endpoint numbers. Give each physical board a distinct serial number, or use `nullptr` for the chip-derived default.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[SerialMulti.ino](SerialMulti.ino)
