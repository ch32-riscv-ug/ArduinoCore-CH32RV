# UdpEcho

[日本語](README.ja.md)

A UDP server on port 9000 that sends each datagram back to its sender, and prints who sent how much.

## Operation

The address appears as `UDP echo on 192.168.x.y:9000`. From a PC run `nc -u 192.168.x.y 9000` and type lines; each comes back and the board prints `#n address:port size`. A datagram longer than 512 bytes comes back cut to 512, and the log says so.

## API and adaptation

Unlike TCP, UDP keeps the datagram as sent: one `parsePacket()` is one datagram, and `read()` gives at most the buffer's size of it (the rest is dropped). There is no delivery guarantee: a datagram or its reply can be lost, so a protocol on UDP needs its own retries. `beginPacket()` / `write()` / `endPacket()` send one datagram; `endPacket()` is false when it could not be sent.

## Before running

Select a board with an Ethernet MAC (the generated profile is CH32V307; V208, V317, V407 / V467 and H415-H417 work the same way) and connect its RJ45 to a network with a DHCP server. Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not carry this UART.

The network runs inside `delay()` and after each `loop()`; the sketch never calls a poll function. A successful build does not verify the cable or the network. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
