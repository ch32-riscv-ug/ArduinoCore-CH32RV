# Network

IPv4 networking on lwIP, with the API of ESP32's Arduino core 3.x Network library (`Network`, `NetworkInterface`, `NetworkClient`, `NetworkServer`, `NetworkUDP`). The plan is in [docs/network-stack.ja.md](../../docs/network-stack.ja.md).

## Targets and status

- For the series with an Ethernet MAC: CH32V208, V203RB, V307/V317, V407/V467, H415-H417. Other series stop with an error.
- Ethernet (`ETH`) runs on a CH32V307 (its built-in 10 Mbit/s PHY), a CH32V407 (45 Mbit/s and TLS 1.3 at 200 MHz) and a CH32H417, checked with the bench's `eth` case: DHCP, ICMP, UDP and TCP echo, TCP both ways, HTTP and HTTPS. V208 / V203RB / V317 / V467 / H415 / H416 are built only.
- USB NCM (`NetworkUsbNcm`, `CH32RVUsbDeviceNet::beginNetwork()` ...) is built for these series, not yet run on one.
- Examples: [EthernetStatus](examples/EthernetStatus/README.md), [TcpEchoServer](examples/TcpEchoServer/README.md), [UdpEcho](examples/UdpEcho/README.md), [HttpGet](examples/HttpGet/README.md), [MdnsWebServer](examples/MdnsWebServer/README.md), [NetworkTime](examples/NetworkTime/README.md) - each run on the V307.
- Tested on the host against Linux's TCP/IP (`tests/unit/network`): static addresses, the DHCP client and server, SNTP, mDNS, ICMP, UDP, TCP both ways, in parallel and outgoing, DNS - also over a link that loses and reorders frames.

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

## Time (SNTP) and mDNS

```cpp
configTime(9 * 3600, 0, "pool.ntp.org");   // Japan; configTzTime("JST-9", ...) is the same
struct tm t;
if (getLocalTime(&t)) { /* ... */ }

MDNS.begin("sensor");                       // sensor.local
MDNS.addService("http", "tcp", 80);         // _http._tcp, seen by browsers and Bonjour / Avahi
MDNS.addServiceTxt("http", "tcp", "path", "/");
```

- SNTP sets the core's clock (`settimeofday()`) and resyncs every hour; `time()`, `localtime_r()` and `strftime()` read it.
- mDNS answers on every interface the library has started, including later ones. Up to 4 services, 96 bytes of TXT each. No queries (`queryHost()` ...) yet.
- Size (V208): using both adds about 26 KB of flash (lwIP's mDNS 12 KB, newlib's time and time-zone code ...). Sketches that do not use them carry neither.

## Common libraries

These work unchanged, tested on the host (2026-10-21):

- MQTT: PubSubClient and ArduinoMqttClient, also over TLS when given a `NetworkClientSecure`.
- HTTP client: ArduinoHttpClient, over HTTP and HTTPS.
- HTTP server: aWOT, handed the connections `NetworkServer::accept()` returns.

Over TLS, connect by host name: the certificate is checked against it.

## Behaviour

- lwIP runs without an OS (NO_SYS), after each `loop()` and inside `delay()` (the core's `ch32rv_net_task()`). A `loop()` that blocks for long stalls the network too.
- Calls that wait (`connect()`, `write()`, `Network.hostByName()`) keep the stack running. `available()`, `read()`, `peek()`, `connected()`, `parsePacket()` and `accept()` run it once when they have nothing, so Stream's timed reads (`readStringUntil()`, `readBytes()` ...) keep receiving.
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
