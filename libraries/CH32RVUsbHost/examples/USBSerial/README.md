# USBSerial

日本語: [README.ja.md](README.ja.md)

A bidirectional UART-console bridge to a CDC-ACM device or supported USB-UART adapter.

## Hardware and connections

A CDC-ACM, FTDI, CP210x, CH34x or PL2303 device, powered host wiring and a 115200-baud UART console. For adapter loopback, connect its UART TX to RX.

## Steps and expected results

1. Check the console for `connected:`.
2. Console characters are sent to the USB serial device.
3. Received characters appear on the console; a TX/RX loopback adapter returns the bytes you send.

## Settings and limits

Serial is the log UART; port is the hosted USB serial stream. begin(115200) stores settings before connection. The first matching port is selected unless setAddress/setPort pins it. Instead of setRxBufferSize, port.setBuffer(array, size) makes a sketch array the receive ring (default 128 bytes). Handle partial write results when adapting this byte bridge to bulk traffic.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[USBSerial.ino](USBSerial.ino)
