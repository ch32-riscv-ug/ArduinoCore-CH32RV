# CustomHID

日本語: [README.ja.md](README.ja.md)

A custom report descriptor defining an eight-byte input-only HID report.

## Hardware and connections

A PC application that reads raw HID reports and a UART monitor.

## Steps and expected results

1. Open usage page `0xff00`, usage 1.
2. About once a second read `45 53 50 counter timeLo timeHi aa 55`.
3. Compare the counter with UART `CUSTOM_HID_TX`.

## Settings and limits

There is no report ID and only an eight-byte Input report is declared. Output/Feature support requires descriptor and callback changes. Keep the constructor length, Report Count and transmitted array length consistent.

## API usage and adaptation

| API or definition | Use in this example |
|---|---|
| `REPORT_DESCRIPTOR` | Defines host interpretation. Report Size is in bits; Report Count counts elements. |
| `CH32RVUsbDeviceHidCustom(...)` | Takes the descriptor pointer, its byte length, and Input payload length. Keep the descriptor alive during use. |
| `customHid.sendReport(report, sizeof(report))` | Sends payload matching the descriptor and returns success as `bool`. |

Here, eight elements of eight bits total eight bytes. Change Report Count, constructor length and sent array length together. If introducing a Report ID, update host handling too.

An Input-only descriptor does not provide Output reception. Add Output/Feature declarations and handlers, or start from [VendorHID](../VendorHID/README.md) for bidirectional communication.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[CustomHID.ino](CustomHID.ino)
