# VendorHID

日本語: [README.ja.md](README.ja.md)

Vendor-defined HID input, output and feature reports, distinct from the bulk-based USBVendor example.

## Hardware and connections

A PC HID application such as a hidapi client and a UART monitor, with report-ID and usage-page selection.

## Steps and expected results

1. Open the HID with report ID 6 and usage page `0xff00`.
2. A 63-byte payload arrives about once a second: `45 53 50` (`ESP`), followed by counter bytes and other data.
3. Send output or feature reports to see `VENDOR_OUTPUT` / `VENDOR_FEATURE` hex logs on UART.

## Settings and limits

Output reports are logged, not echoed. See [CustomDevice](../CustomDevice/README.md) for echo. Distinguish the host API report-ID byte from the 63-byte payload. The `ESP` prefix is retained test data from the original sketch, not a device-type indication.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[VendorHID.ino](VendorHID.ino)
