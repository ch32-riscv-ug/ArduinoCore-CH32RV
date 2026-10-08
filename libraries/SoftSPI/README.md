# SoftSPI

日本語: [README.ja.md](README.ja.md)

Bit-banged SPI on any three pads, with the same API as [SPI](../SPI).

```cpp
#include <SoftSPI.h>

SoftSPI bus(PA1, PA4, PA2);      // SCK, MISO, MOSI (SPI.begin()'s order)
```

## Why

The hardware SPI can only reach the pads its routes name. On the small CH32
parts those are often gone - not bonded out on your package, or already
carrying something else. A CH32V003 in SOP8 has six GPIO in total.

This is the way out. It uses nothing but `pinMode()` and `digitalWrite()`, so
it goes anywhere a pin goes.

## Drop-in

`SoftSPI` derives from `HardwareSPI`, the same base `SPI` uses, so anything
written against `SPIClass&` takes one unchanged:

```cpp
SoftSPI bus(PA1, PA4, PA2);
Adafruit_Something device(&bus);
```

## What it does not do

- **No clock frequency.** `SPISettings`' frequency is accepted and ignored:
  a bit-banged bus cannot hit a number. Nothing breaks - SPI is clocked by the
  controller, so a slow clock only means a slow transfer. `setHalfPeriodUs()`
  is the one knob that changes the speed, and it sets a *floor* on the half
  period, for long wires or a device that needs a slower clock.
- **No chip select**, exactly as in `SPI`: Arduino drives CS as an ordinary
  GPIO, which is what lets one bus carry several devices.
- **No peripheral (slave) mode.** A slave has to follow someone else's clock,
  which a busy loop cannot promise. `SPI_HAS_PERIPHERAL_MODE` stays undefined.

## Modes

All four. `SPI_MODE0` through `SPI_MODE3`, and `MSBFIRST` / `LSBFIRST`, via
`beginTransaction(SPISettings(...))` or the older `setDataMode()` /
`setBitOrder()`.

## Cost

Each edge calls `digitalWrite()`, and deriving from `HardwareSPI` retains its
overrides, so `SoftSPI` is not necessarily smaller than the hardware driver.
Use the build size report for the exact cost. It is not linked into sketches
that do not include its header.
