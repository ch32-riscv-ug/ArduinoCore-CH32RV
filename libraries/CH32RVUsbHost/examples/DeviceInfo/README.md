# DeviceInfo

日本語: [README.ja.md](README.ja.md)

UART device identity, strings, interfaces and endpoints: the first check for host wiring.

## Hardware and connections

An exact host-capable part, a host adapter providing 5-V VBUS, any USB device and a 115200-baud UART monitor.

## Steps and expected results

1. Plug in a device to see `connected:` and descriptor details.
2. Inspect VID:PID, speed, interface classes and endpoint directions/packet sizes.
3. Unplug it to see `disconnected:`.
4. For composite devices, choose the next example from its interface classes.

## Settings and limits

This sketch prints on connect/disconnect; it does not implement every periodic display or diagnostic in the Esp example. For Unsupported, check the exact chip part first. Enumeration alone does not mean its class operations are supported.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbHost API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[DeviceInfo.ino](DeviceInfo.ino)
