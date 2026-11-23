# UsbHostNetwork

[日本語](README.ja.md)

The board as the USB host of a USB network device (CDC-NCM or CDC-ECM), with the IP stack on top
(`CH32RVUsbHost::networkAttachNetif()`, EspUsbHost's call). It takes an address by DHCP and fetches the
gateway's page by HTTP every 10 s, printing the status line.

## What you need and the wiring

- A board with a USB host port (the series without a MAC too: X035, L103 and other 64 KB / 20 KB parts take about 57 KB of flash).
- A CDC-NCM or CDC-ECM device: an ESP32-S3 / P4 running EspUsbDevice's `UsbNetwork` example (a DHCP server and a
  web server at 192.168.7.1), a CH32RV board running `UsbNcmNetwork`, or an NCM / ECM USB Ethernet adapter.
- `Serial` at 115200 bps.

## What to do and what to expect

Flash it and plug the device in; with EspUsbDevice's `UsbNetwork` on the other end:

```
USB network device attached, asking DHCP
... 192.168.7.2 ... (MAC, address, gateway)
GET http://192.168.7.1/ -> HTTP/1.1 200 OK
```

For a device without a DHCP server, set `CH32RVUsbHostNetworkConfig`'s `dhcpClient = false` and `ip` /
`gateway` / `subnet` for a fixed address.

## Notes

- One device at a time. NCM receive blocks up to 3200 bytes (a device offering larger ones is asked for smaller).
- While `networkAttachNetif()` holds it, the frames are the interface's: no `onNetworkFrame()` / `networkReadFrame()`.
- Checked with V407 and an ESP32-P4 (EspUsbDevice's NCM): DHCP and a TCP round trip (2026-11-22, usb-peer-host-net).
