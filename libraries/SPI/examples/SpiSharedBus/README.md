# SpiSharedBus

[日本語](README.ja.md)

Two devices of different speed and mode (an SPI Flash and a sensor) on one SPI bus (SCK, MISO, MOSI), each picked by its own CS and read in turn.

## Wiring and operation

SCK / MISO / MOSI to both devices, each CS to the sketch's `FLASH_CS` / `SENSOR_CS`, supply and GND. Every second it prints `flash EF 40 18, sensor 0x6A` and the like (the values are the devices').

## API and notes for changes

Each device keeps its `SPISettings` (speed, bit order, mode); `beginTransaction()` switches to it before its CS goes low, and after the transfer CS goes high and `endTransaction()`. Both CS high before `begin()` keeps either device from being selected during start-up. If an interrupt uses the same bus, guard it with `SPI.usingInterrupt()`.

## Before running

Choose the board and the exact Part Number and check the pads against the schematic. The generated `sketch.yaml` has the build profiles; a build that passes is not a check of the wiring or of the board.
