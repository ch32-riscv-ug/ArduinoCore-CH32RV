# HttpGet

[日本語](README.ja.md)

Fetch a page over plain HTTP every 30 seconds by host name, and print the status code, the body size and the time. Each kind of failure is told apart: the name does not resolve, the connection is refused, no answer before the deadline.

## Operation

With the default `example.com` the log shows `example.com (address): HTTP 200, N bytes in T ms`. Set `HOST` to a PC on your network running `python3 -m http.server 80` to try it without the Internet. Try a name that does not exist (`DNS: cannot resolve`) and a port nothing listens on (`Connect to ... failed`).

## API and adaptation

`Network.hostByName()` looks the name up first (a dotted address also works), so a DNS failure is not mistaken for a refused connection. The request is HTTP/1.0: the body comes unchanged (no chunked encoding) and the server closes the connection at its end. Reading stops at `TIMEOUT_MS` even if the server never closes. For HTTPS use `NetworkClientSecure` in place of `NetworkClient`, with the server's CA certificate.

## Before running

Select a board with an Ethernet MAC (the generated profile is CH32V307; V208, V317, V407 / V467 and H415-H417 work the same way) and connect its RJ45 to a network with a DHCP server. Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not carry this UART.

The network runs inside `delay()` and after each `loop()`; the sketch never calls a poll function. A successful build does not verify the cable or the network. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
