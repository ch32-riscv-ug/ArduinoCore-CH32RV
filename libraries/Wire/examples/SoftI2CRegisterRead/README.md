# SoftI2CRegisterRead

[日本語](README.ja.md)

Reads a register of an I2C device on any two GPIO pads, for when the hardware I2C's pads are taken or have no route. `SoftWire` is `Wire`'s API done in software.

## Wiring and operation

The device's SDA / SCL to the sketch's `SDA_PIN` / `SCL_PIN`, supply and GND, pull-ups of about 4.7 kΩ on SDA and SCL. Every second it prints `register 0x75 = 0x68` and the like (an MPU-6050's WHO_AM_I); with no device, `no answer, code 2`.

## API and notes for changes

After `SoftWire bus(sda, scl)` it is `Wire`. `endTransmission(false)` keeps the bus and the following `requestFrom()` sends a repeated START. `setClock()` sets the clock; in software, above 400 kHz depends on the series' clock. A long interrupt stretches the clock's low by as much (harmless on I2C).

## Before running

Choose the board and the exact Part Number and check the pads against the schematic. The generated `sketch.yaml` has the build profiles; a build that passes is not a check of the wiring or of the board.
