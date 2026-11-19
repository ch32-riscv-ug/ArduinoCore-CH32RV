# I2CEeprom

[日本語](README.ja.md)

Writes a string to a 24C02-style I2C EEPROM, reads it back and prints it on `Serial`: splitting writes at page boundaries, and waiting out the internal write cycle by ACK polling.

## Wiring and operation

The EEPROM's SDA / SCL to the board's `SDA` / `SCL`, VCC and GND to the supply; A2 .. A0 to GND (address 0x50), WP to GND (writable); pull-ups of about 4.7 kΩ on SDA and SCL (modules often carry them). `written, read back: Hello, EEPROM!` means it worked.

## API and notes for changes

One write transaction stays inside a page (8 bytes on 24C02, 16 on 24C04 .. 16); past it the address wraps to the page's start, so `writeBytes()` splits. For about 5 ms after a write the chip does not answer its address: `endTransmission()` is retried until it returns 0. 24C32 and up take a two-byte address: `Wire.write(at)` becomes two writes, high byte first.

## Before running

Choose the board and the exact Part Number and check the pads against the schematic. The generated `sketch.yaml` has the build profiles; a build that passes is not a check of the wiring or of the board.
