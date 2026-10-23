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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `vendor.sendInput(payload, length)` | Sends Device-to-Host Input payload without a Report ID byte in the supplied array. |
| `onOutputReport()` | Receives Host-to-Device Output reports through `deliverEvents()`. |
| `onFeatureReport()` | Receives host-set Feature reports; it does not persist settings automatically. |
| `report.reportId` / `report.length` / `report.data` | Identify the report and its payload length/data. Do not assume a string. |

Some host APIs require a Report ID at the start of their write buffer. Check the descriptor and host API conventions; do not add an ID twice to a device `sendInput()` payload. Copy callback data if it must survive the call.

When adding commands, define command identifiers and lengths, then check `report.length` before reading. Received data is not necessarily NUL-terminated; do not pass it directly to `%s`.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[VendorHID.ino](VendorHID.ino)
