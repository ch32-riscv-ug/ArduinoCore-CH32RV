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

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[CustomHID.ino](CustomHID.ino)
