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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `Console(device, "Console")` / `DataLink(device, "Data Link")` | Add independently named CDC functions to the same device. |
| Each port's `available()` / `read()` / `connected()` | Data and DTR are independent; opening one port does not open the other. |
| `port()` | Returns the CDC index inside the device, not an OS COM number or `/dev/ttyACM*` suffix. |
| `device.begin(config)` | Starts the complete device after declarations and callback registration. |

Open two terminals and identify the `CONSOLE:` and `DATALINK:` replies. Use interface names and replies rather than assuming OS port order. Use distinct device serial numbers when connecting multiple boards.

For a machine protocol, remove the periodic `data tick` output and per-character echo from DataLink. Otherwise the peer's parser will treat human-readable logs as protocol input.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[SerialMulti.ino](SerialMulti.ino)
