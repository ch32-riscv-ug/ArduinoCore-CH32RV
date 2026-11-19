# SoftSpiDevice

[日本語](README.ja.md)

An SPI device on any four GPIO pads (here an SPI Flash's JEDEC ID), for when the hardware SPI's pads are taken. `SoftSPI` is `SPI`'s API done in software.

## Wiring and operation

The Flash's SCK, MISO (DO), MOSI (DI) and CS to the sketch's four pads, supply and GND, WP and HOLD to VCC. Every second it prints `JEDEC ID: EF 40 18` and the like (a W25Q128); with nothing there, `FF FF FF` or `00 00 00`.

## API and notes for changes

After `SoftSPI bus(sck, miso, mosi)` it is `SPI`: `SPISettings`' mode and bit order apply. The speed is software's, set by the series' clock, and may come out slower than asked. CS is the sketch's to drive.

## Before running

Choose the board and the exact Part Number and check the pads against the schematic. The generated `sketch.yaml` has the build profiles; a build that passes is not a check of the wiring or of the board.
