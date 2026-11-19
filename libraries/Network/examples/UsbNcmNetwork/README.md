# UsbNcmNetwork

[日本語](README.ja.md)

The board as a USB network adapter (CDC-NCM). The PC gets an address from the board's DHCP server; opening
`http://192.168.7.1/` in a browser shows the board's uptime and the PC's address. Link up and down are logged on the UART.

## What you need and the wiring

- A part with an Ethernet MAC and a USB device port (V307, V407, H417 and the like: the series the Network library is
  for), and a USB cable to the PC.
- A UART monitor at 115200 bps, 8N1, on `RX` / `TX`.
- On the PC: Linux and macOS take it as an NCM adapter; Windows 10 and later work with their own NCM driver.

## What to do and what to expect

After the upload, with USB connected, the PC has one more wired network adapter with an address such as 192.168.7.2.
The UART shows:

```
USB network up: open http://192.168.7.1/
link up (the PC configured the adapter)
served 192.168.7.2
```

The browser (or `curl http://192.168.7.1/`) gets:

```
Hello from the board over USB.
Uptime: 42 s
Your address: 192.168.7.2
```

## The API and the flow

- `CH32RVUsbDeviceNet ncm` is the USB NCM function. `dhcpServer(true)`, then `USBDevice.begin()`, then
  `ncm.beginNetwork()` bring the IP interface up.
- No gateway or DNS is offered (unless `dhcpAdvertiseGateway()` is called), so the PC keeps its default route.
- `ncm.networkInterface()` is the Network library's interface; `linkUp()` is true once the PC has configured the adapter.
- The web server is the same `NetworkServer` / `NetworkClient` as on Ethernet.
