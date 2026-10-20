# VideoCamera

日本語: [README.ja.md](README.ja.md)

A sensor-free UVC device generating YUY2 color bars and a moving white block.

## Hardware and connections

A PC UVC application able to select 80×60 YUY2, plus a UART monitor.

## Steps and expected results

1. Select the USB Camera in a UVC application.
2. Open 80×60 YUY2 at 10 fps; bulk streaming is the default.
3. Check that the white block at the bottom moves, not just that the bars are visible.
4. Inspect UART `Host committed`, `Streaming started` and `Streaming stopped`.

## Settings and limits

Each frame is 9600 bytes, generated per payload by onFillPayload rather than stored whole in RAM. Loop schedules sendGeneratedFrame; completion callbacks stay short. To try ISO, call `Camera.setTransferType(CH32RVUsbDeviceVideoTransfer::Isochronous)` before `device.begin()`. X035 at 64 B/ms cannot sustain 10 fps including headers, so reduce the advertised rate to match capacity.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[VideoCamera.ino](VideoCamera.ino)
