# SerialUSBEcho

日本語: [README.ja.md](README.ja.md)

A minimal CDC ACM device using the predefined `SerialUSB`. It echoes received bytes and sends a counter once a second while the terminal is open.

## Hardware and connections

A supported device board, a USB data cable and a PC serial terminal. This sketch does not use a separate UART.

## Steps and expected results

1. Upload and connect the board USB data port to the PC.
2. Open the new CDC port with DTR enabled, using a baud rate other than 1200.
3. Send `hello`; the same bytes return, alongside `count 0`, `count 1`, etc. once a second.
4. Closing the terminal stops periodic output; reopening resumes it.

## Settings and limits

`SerialUSB.begin()` uses the default `1209:0001` identity. Newlines pass through unchanged; disable terminal local echo to avoid seeing duplicates. Its boolean conversion reflects DTR. Opening and closing at 1200 bps triggers the upload entry; see the guide for the X035/X033-specific ISP behavior.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `SerialUSB.begin()` | Starts the predefined `USBDevice` and CDC port. The optional baud argument does not configure a UART clock. |
| `available()` / `read()` | Check buffered bytes and read one at a time. Keep the result as `int` initially: `read()` returns `-1` when empty. |
| Boolean conversion / `connected()` | Check host DTR, rather than merely whether a cable is attached. |
| `write()` / `print()` / `println()` | Send raw bytes, formatted text, or text followed by a newline. Use `write()` for a binary bridge. |

Polling the receive buffer does not require `deliverEvents()`. If you register `onRx()` instead, deliver notifications from `loop()` as shown in [SerialEvents](../SerialEvents/README.md).

The echo operates on bytes, not lines. Disable terminal local echo to distinguish received replies from local display. CDC writes can wait if the host keeps the port open without reading; place periodic output inside `if (SerialUSB)`.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[SerialUSBEcho.ino](SerialUSBEcho.ino)
