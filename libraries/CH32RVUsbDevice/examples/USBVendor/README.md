# USBVendor

日本語: [README.ja.md](README.ja.md)

A non-HID vendor interface with bulk IN/OUT, EP0 vendor requests and WebUSB/WinUSB descriptors.

## Hardware and connections

A PC libusb/WinUSB/WebUSB client or the Host VendorBulk example on another board, plus a 115200-baud UART monitor.

## Steps and expected results

1. Open the vendor interface with class 0xff; discover endpoint addresses from its descriptors.
2. Send hello on bulk OUT; bulk IN returns echo: hello followed by CRLF.
3. Bulk IN also carries `status rx=... control=...` about every three seconds.
4. Vendor IN request 0x01 returns CH32RVUsbDeviceVendor. OUT request 0x02 is a no-data status acknowledgement.

## Settings and limits

The RX callback reads at most 64 bytes at a time and prefixes each chunk with echo:. Bulk USB does not preserve application line boundaries. IN mixes echo and periodic status, so the first read is not necessarily the echo. The WebUSB URL is placeholder text, not a supplied web application. Use your own browser client/permission flow. A serial terminal cannot open this interface; check Linux access permissions or Windows WinUSB binding.

## PC check with PyUSB

Use Python with PyUSB, a libusb backend and permission to open the device.
Add serial-number selection when multiple devices share the test VID/PID.
Discover the vendor interface and endpoints rather than using the DFU interface.

```python
import usb.core
import usb.util

dev = usb.core.find(idVendor=0x1209, idProduct=0x0001)
if dev is None:
    raise RuntimeError("USBVendor device not found")
cfg = dev.get_active_configuration()
itf = next(i for i in cfg if i.bInterfaceClass == 0xff)
ep_in = next(e for e in itf if e.bEndpointAddress & 0x80)
ep_out = next(e for e in itf if not e.bEndpointAddress & 0x80)

usb.util.claim_interface(dev, itf.bInterfaceNumber)
try:
    info = dev.ctrl_transfer(0xc1, 0x01, 0, itf.bInterfaceNumber, 64)
    print(bytes(info).decode("ascii"))  # CH32RVUsbDeviceVendor
    ep_out.write(b"hello", timeout=2000)
    print(bytes(ep_in.read(512, timeout=2000)))  # echo or periodic status
    dev.ctrl_transfer(0x41, 0x02, 0, itf.bInterfaceNumber, b"")
finally:
    usb.util.release_interface(dev, itf.bInterfaceNumber)
    usb.util.dispose_resources(dev)
```

0xc1/0x41 select vendor interface IN/OUT requests. For access/claim failures,
check host permissions, driver binding and competing applications. Read again
if queued status messages precede the echo.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[USBVendor.ino](USBVendor.ino)
