# CustomDevice

日本語: [README.ja.md](README.ja.md)

A configurable composite device combining CDC, DFU runtime and Vendor HID, with a HID output-to-input echo.

## Hardware and connections

A PC CDC terminal, a HID report application and a 115200-baud UART monitor.

## Steps and expected results

1. Check UART for `USB started` and find CDC and HID on the PC.
2. CDC echoes the bytes it receives.
3. Send a Vendor HID output report with report ID 6; it is returned as an input report.
4. UART logs `host configured the device` and `host gone` as mount state changes.

## Settings and limits

Vendor HID payloads are at most 63 bytes, with report ID 6. Check whether the host HID API expects the report ID at the start of its buffer. `serialNumber=nullptr` selects the chip-derived identity. `maxPowerMilliamps=100` is a descriptor declaration, not power-regulator control.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[CustomDevice.ino](CustomDevice.ino)
