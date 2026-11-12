# Network

IPv4 networking on lwIP, with the API of ESP32's Arduino core 3.x Network library (`Network`, `NetworkInterface`, `NetworkClient`, `NetworkServer`, `NetworkUDP`). The design is in [docs/network-stack.ja.md](../../docs/network-stack.ja.md).

## Targets and status

- For the series with an Ethernet MAC: CH32V208, V203RB, V307/V317, V407/V467, H415-H417. Other series stop with an error.
- Ethernet (`ETH`) runs on a CH32V307 (its built-in 10 Mbit/s PHY), a CH32V407 (45 Mbit/s and TLS 1.3 at 200 MHz), a CH32H417 and a CH32V208 (its built-in 10 Mbit/s PHY, CPU Clock `60MHz`, no TLS), checked with the bench's `eth` case: DHCP, ICMP, UDP and TCP echo, TCP both ways, HTTP and HTTPS. V203RB / V317 / V467 / H415 / H416 are built only.
- USB NCM (`NetworkUsbNcm`, `CH32RVUsbDeviceNet::beginNetwork()` ...) is built for these series and has not been run on hardware.
- Examples: [EthernetStatus](examples/EthernetStatus/README.md), [TcpEchoServer](examples/TcpEchoServer/README.md), [UdpEcho](examples/UdpEcho/README.md), [HttpGet](examples/HttpGet/README.md), [MdnsWebServer](examples/MdnsWebServer/README.md), [NetworkTime](examples/NetworkTime/README.md) - each run on the V307.
- Tested on the host against Linux's TCP/IP (`tests/unit/network`): static addresses, the DHCP client and server, SNTP, mDNS, ICMP, UDP, TCP both ways, in parallel and outgoing, DNS - also over a link that loses and reorders frames.

## Usage

```cpp
#include <Network.h>
// a driver such as ETH provides the NetworkInterface
NetworkServer server(80);

void setup() {
  ETH.begin();                 // DHCP; for a static address call ETH.config(ip, gw, mask, dns) first
  while (!ETH.hasIP()) delay(100);
  server.begin();
}

void loop() {
  NetworkClient client = server.accept();
  if (client) {
    client.print("HTTP/1.0 200 OK\r\n\r\nhello\r\n");
    client.stop();
  }
}
```

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
- mDNS answers on every interface the library has started, including later ones. Up to 4 services, 96 bytes of TXT each. There are no queries (`queryHost()` ...).
- Size (V208 at 60 MHz, 2026-11-12): using both adds about 23 KB of flash (lwIP's mDNS 12 KB, SNTP with the time and time-zone code 11 KB). Sketches that do not use them carry neither.

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
- IPv4 only. 127.0.0.1 and the interface's own address loop back inside the board (a gateway and its client on one chip).

## Size (CH32V208, 2026-10-21)

For a sketch using DHCP, a TCP server and client, UDP and DNS, the network adds about 44 KB of flash (lwIP about 37 KB, this library about 6 KB) and about 22 KB of static RAM (lwIP's 10 KB transmit heap, 9 KB receive pool ...), plus 2.9 KB per open connection. The V407 and H41x get larger windows and pools (about 60 KB static; 12 TCP connections instead of 6), and so do the V307/V317 with 128 KB of RAM or more in the Flash / RAM Split menu (TcpEchoServer: 37 KB of static RAM at 64 KB, 77 KB at 128 KB). The configuration is `src/lwipopts.h`.

## Calling lwIP directly

A library may use lwIP's raw API itself (`tcp_*`, `udp_*`, `pbuf_*`, `sys_timeout()` ...), as an MQTT broker or a VPN does:

- Call `Network.begin()` first (an interface's `begin()` does it too): it starts lwIP and has the core run it after each `loop()` and inside `delay()`.
- Call lwIP only in task context - `setup()`, `loop()`, and lwIP's own callbacks, `sys_timeout()`'s included, which run in that same task. Never from an interrupt: lwIP is built without an OS (NO_SYS) and is not reentrant.
- What stays the same is this calling context. lwIP's version and `src/lwipopts.h` (pools, connection counts, per series and RAM) may change with a core release.

## Writing a link driver

Derive from `NetworkInterface`; everything is called in task context, never from an interrupt:

- `transmitFrame(frame, length)` sends one Ethernet frame.
- Received frames go to `frameReceived(frame, length)` - from `poll()`, which the task calls, draining what the interrupt queued.
- `setLinkUp(up)` reports the link.
- `netifStart(mac)` to start, `netifStop()` to stop.

## License

lwIP is BSD-3-Clause (`src/lwip-src/COPYING`); the vendored release and file hashes are in `vendor/lwip.lock.toml`.
