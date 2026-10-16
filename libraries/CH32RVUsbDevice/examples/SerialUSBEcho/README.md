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

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[SerialUSBEcho.ino](SerialUSBEcho.ino)
