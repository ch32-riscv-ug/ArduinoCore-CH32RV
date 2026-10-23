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

## API usage and adaptation

| API | Use in this example |
|---|---|
| `onDeviceConnected()` | Flags a later open attempt, keeping class work in `loop()`. |
| `vendorOpen(address, interfaceNumber)` | Selects and opens a vendor interface; omitted arguments select a candidate automatically. |
| `onVendorData(callback)` | Receives IN data through `deliverEvents()`; copy data rather than retaining its pointer. |
| `vendorWrite(buffer, length)` | Sends OUT and returns success. Check `lastErrorName()` on failure. |

Callback chunks are not necessarily complete application messages. Define delimiters or length fields and assemble multiple receives. IN/OUT directions are host-relative.

Connection events also occur for non-vendor devices. Select the intended interface and accept UART input after a successful open to avoid consuming data while disconnected. Add a retry policy for failed sends.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[VendorBulk.ino](VendorBulk.ino)
