# SoftwareSerialExample

[日本語](README.ja.md)

Bridge the hardware `Serial` and a SoftwareSerial on two ordinary pins.

## Hardware and connections

A USB-UART adapter on `Serial` (adapter RX to board TX, TX to RX, common GND) for the terminal, and a second device
(or a second adapter) at 9600 8N1 on the SoftwareSerial: its RX to `SOFT_TX` (A2 in the sketch), its TX to `SOFT_RX`
(A1), common GND. Match I/O voltages.

## Steps and expected results

Open the terminal on `Serial` at 115200. What you type goes out on `SOFT_TX`; what the device sends on `SOFT_RX`
appears in the terminal. With the two SoftwareSerial pins joined together nothing comes back: sending and receiving do
not overlap (half duplex).

## API and processing flow

`soft.begin(9600)` starts it and makes it the listening object. `write()` returns once the byte is out, so the bridge
paces itself. `overflow()` reports bytes dropped because the 64-byte buffer was full.

## Adapting the example

Change `SOFT_RX` / `SOFT_TX` to any pads, and the speed or format (`begin(9600, SERIAL_8E1)`). Above 115200 the receive
side may lose bits on the slower parts. Use a hardware `Serial` when one reaches your pins.

## Related documentation

- [Sketch](SoftwareSerialExample.ino)
- [SoftwareSerial](../../README.md)
