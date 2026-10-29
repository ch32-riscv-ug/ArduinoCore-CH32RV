# Serial

日本語: [README.ja.md](README.ja.md)

An explicitly declared CDC port with UART diagnostics for line coding, DTR/RTS and received data.

## Hardware and connections

A PC CDC terminal and a separate 115200-baud 8N1 UART monitor on the board RX/TX pins.

## Steps and expected results

1. Open the UART monitor and connect USB.
2. Open the CDC port; UART reports `CDC_LINE_CODING` and `CDC_LINE_STATE` changes.
3. Send `abc`; each character returns in its own `echo: a`, `echo: b`, `echo: c` line.
4. An open CDC port receives `tick=... line_coding=... line_state=...` about every three seconds.

## Settings and limits

`onRx()` reports availability; `loop()` consumes the bytes. CR is ignored and LF becomes CRLF. `UsbSerial` is CDC and `Serial` is UART. CDC baud is host-provided line coding, not USB bus speed.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceCdcSerial UsbSerial(device)` | Adds CDC to this device. `Serial` is UART diagnostics; `UsbSerial` communicates over USB. |
| `device.begin(config)` | Starts the device after functions and configuration are ready. On `false`, print `device.lastErrorName()` on UART. |
| `onLineCoding()` / `onLineState()` | Observe host settings and DTR/RTS through `deliverEvents()`. |
| `onRx()` | Notifies the sketch of received data. It does not read the data; this example reads it in `loop()`. |
| `flush()` | Advances pending transmission; it does not clear received data. |

Successful startup, host configuration (`device.ready()`), and an open CDC port (`UsbSerial.connected()`) are separate states. Check DTR and the selected port if periodic output is missing. Host baud settings do not change the hardware UART; a bridge must apply them explicitly if needed.

The sample discards CR, converts LF to CRLF, and prefixes each other character with `echo: `. Remove this text processing for binary forwarding, and handle the returned write lengths.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Serial.ino](Serial.ino)
