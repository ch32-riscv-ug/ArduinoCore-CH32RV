# MdnsWebServer

[日本語](README.ja.md)

A small web server on port 80 that a PC can open as `http://ch32rv.local/` without knowing the address: the name is announced over mDNS. The page shows the uptime and how often it was served; any other path answers 404.

## Operation

The log shows `http://ch32rv.local/  (192.168.x.y)`. Open that in a browser on the same network (macOS, iOS, Windows 10 and later, and Linux with Avahi resolve `.local` names), or use the address. Each request is logged with the client's address and path. The service also appears as `_http._tcp` in Bonjour / Avahi browsers.

## API and adaptation

`MDNS.begin(name)` after the address is set; `MDNS.addService("http", "tcp", 80)` makes the server discoverable. Choose a name unique on your network. The request reader stops at `REQUEST_TIMEOUT_MS`, so a client that connects and sends nothing does not hold the server. It answers HTTP/1.0 and closes after each page; one client is handled at a time.

## Before running

Select a board with an Ethernet MAC (the generated profile is CH32V307; V208, V317, V407 / V467 and H415-H417 work the same way) and connect its RJ45 to a network with a DHCP server. Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not carry this UART.

The network runs inside `delay()` and after each `loop()`; the sketch never calls a poll function. A successful build does not verify the cable or the network. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
