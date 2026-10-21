# Network

IPv4 networking on lwIP, with the API of ESP32's Arduino core 3.x Network library (`Network`, `NetworkInterface`, `NetworkClient`, `NetworkServer`, `NetworkUDP`). The plan is in [docs/network-stack.ja.md](../../docs/network-stack.ja.md).

## Targets and status

- For the series with an Ethernet MAC: CH32V208, V307/V317, V407/V467, H415-H417. Other series stop with an error.
- **No Ethernet MAC drivers yet.** USB NCM is here (`NetworkUsbNcm`, `CH32RVUsbDeviceNet::beginNetwork()` ...), but those series have no USB device driver yet, so it does not run on a chip.
- Tested on the host against Linux's TCP/IP (`tests/unit/network`): static addresses, the DHCP client and server, ICMP, UDP, TCP both ways, in parallel and outgoing, DNS - also over a link that loses and reorders frames.

## USB NCM (as with EspUsbDevice)

```cpp
#include <CH32RVUsbDevice.h>
#include <Network.h>

CH32RVUsbDeviceNet usbnet;

void setup() {
  usbnet.dhcpServer(true);   // the PC gets 192.168.7.2 (the board is 192.168.7.1)
  USBDevice.begin();
  usbnet.beginNetwork();
}
```

- The board's end takes `CH32RVUsbDeviceNet::macAddress()` (the PC's end) with the last bit flipped.
- The link is not the board's default route unless `defaultRoute(true)`: it never takes over from Ethernet.
- Frames from the PC pass through a 4 KB queue to lwIP; when it is short of room, USB stops taking blocks and the PC waits - nothing is dropped.

## Behaviour

- lwIP runs without an OS (NO_SYS), after each `loop()` and inside `delay()` (the core's `ch32rv_net_task()`). A `loop()` that blocks for long stalls the network too.
- Calls that wait (`connect()`, `write()`, `Network.hostByName()`) keep the stack running. `available()`, `connected()`, `parsePacket()` and `accept()` run it once when they have nothing.
- Not callable from an interrupt.
- `NetworkClient` copies received data into a buffer of its own (the TCP window's size); the window opens as the sketch reads, so a slow reader slows the sender instead of losing data.
- `write()` waits for room in the send buffer and gives up after 10 s without progress, returning what it wrote.
- The DHCP server (`dhcpServer(true, leases)`) lends `leases` addresses after the static one, an hour each, the same one again to the same MAC. It offers a router (`dhcpAdvertiseGateway()`) and DNS (`dhcpDns()`) only when told to: a device that does not forward would otherwise take over the peer's default route.
- `NetworkUDP` queues up to four received datagrams and drops the rest; one datagram sent is at most 1472 bytes.
- IPv4 only.

## Size (CH32V208, 2026-10-21)

For a sketch using DHCP, a TCP server and client, UDP and DNS, the network adds about 44 KB of flash (lwIP about 37 KB, this library about 6 KB) and about 22 KB of static RAM (lwIP's 10 KB transmit heap, 9 KB receive pool ...), plus 2.9 KB per open connection. The V407 and H41x get larger windows and pools (about 60 KB static). The configuration is `src/lwipopts.h`.

## Writing a link driver

Derive from `NetworkInterface`; everything is called in task context, never from an interrupt:

- `transmitFrame(frame, length)` sends one Ethernet frame.
- Received frames go to `frameReceived(frame, length)` - from `poll()`, which the task calls, draining what the interrupt queued.
- `setLinkUp(up)` reports the link.
- `netifStart(mac)` to start, `netifStop()` to stop.

## License

lwIP is BSD-3-Clause (`src/lwip-src/COPYING`); the vendored release and file hashes are in `vendor/lwip.lock.toml`.
