# EthernetStatus

[日本語](README.ja.md)

Start Ethernet with DHCP and print the MAC address, then each change of the link (speed, duplex) and of the address (IP, mask, gateway, DNS). This is the first step before any of the other network examples.

## Operation

After reset the MAC address appears, then `Link up, 100 Mbit/s full duplex` (or 10) and the address with the time DHCP took. Unplug the cable: `Link down` appears, and plugging it back brings the link and the address back. With no DHCP server, `No DHCP answer yet` appears after 15 seconds and the sketch keeps waiting.

## API and adaptation

`ETH.begin()` starts the driver and DHCP and returns at once; it is false only when the board has no Ethernet or the PHY does not answer. `linkUp()` and `hasIP()` are states to look at, not calls that wait. For a fixed address call `ETH.config(ip, gateway, mask, dns)` before `begin()`.

## Before running

Select a board with an Ethernet MAC (the generated profile is CH32V307; V208, V317, V407 / V467 and H415-H417 work the same way) and connect its RJ45 to a network with a DHCP server. Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not carry this UART.

The network runs inside `delay()` and after each `loop()`; the sketch never calls a poll function. A successful build does not verify the cable or the network. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
