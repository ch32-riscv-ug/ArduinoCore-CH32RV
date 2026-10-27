# TcpEchoServer

[日本語](README.ja.md)

A TCP server on port 7 that sends back whatever a client sends, one client at a time. It shows accepting a client, reading what has arrived, writing when the send buffer takes only part, and noticing the client has gone.

## Operation

The address appears as `Echo server on 192.168.x.y:7`. From a PC on the same network run `nc 192.168.x.y 7` and type lines: each comes back. The board prints the client's address, and `Closed after N bytes` when it disconnects. A client that sends nothing for 60 seconds is closed. A second client waits until the first one leaves.

## API and adaptation

TCP is a byte stream: `read()` returns what has arrived so far, and one `write()` on the other side can arrive in several reads or several writes in one. Put your own message boundaries (a line end, a length) on top. `write()` returns how much the stack took; the rest has to be sent again. A client that has disconnected tests false, so the sketch keeps its own flag to report the close once.

## Before running

Select a board with an Ethernet MAC (the generated profile is CH32V307; V208, V317, V407 / V467 and H415-H417 work the same way) and connect its RJ45 to a network with a DHCP server. Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not carry this UART.

The network runs inside `delay()` and after each `loop()`; the sketch never calls a poll function. A successful build does not verify the cable or the network. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
