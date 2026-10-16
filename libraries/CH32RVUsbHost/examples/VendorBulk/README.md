# VendorBulk

日本語: [README.ja.md](README.ja.md)

A vendor bulk interface bridge: UART input to bulk OUT, and received bulk IN shown as hex.

## Hardware and connections

A USB device with a known bulk IN/OUT protocol; the USBVendor device example on a second board is a useful peer. Supply host VBUS and use a UART console.

## Steps and expected results

1. Check for `vendorOpen: ok` after connection.
2. Send console text.
3. With a USBVendor peer, `echo: ...` returns as `in: ...` hex output, along with periodic status data.

## Settings and limits

The default selects a discovered vendor interface. Knowing VID/PID does not define its protocol; use a known peer rather than sending text to arbitrary vendor devices. Input is logged in hex. Console data is chunked into at most 64 bytes, without preserving application message boundaries.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[VendorBulk.ino](VendorBulk.ino)
