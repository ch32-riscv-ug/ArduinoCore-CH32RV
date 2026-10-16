# UsbNetworkPing

日本語: [README.ja.md](README.ja.md)

A CDC-NCM device answering ARP and IPv4 ping directly from Ethernet frames, without an IP stack, web server or Internet connection.

## Hardware and connections

A PC with CDC-NCM support, a USB data cable and a UART monitor. Profiles require at least 16 KiB RAM.

## Steps and expected results

1. Configure the new PC USB network interface with static IPv4 `192.168.7.2/24`; no gateway or DNS is needed.
2. Run `ping 192.168.7.1`.
3. Check UART `link=... frames=... arp=... ping=...` counters about every two seconds.
4. Bind ping to this interface if the OS routes traffic elsewhere.

## Settings and limits

There is no DHCP. If no NIC appears, check OS NCM support/binding first. Match MY_IP with the PC subnet when changing addresses. Test ordinary small, unfragmented IPv4 ping without IP options: this demo is not a general validated IP parser. The receive callback prepares a response and loop sends it.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[UsbNetworkPing.ino](UsbNetworkPing.ino)
