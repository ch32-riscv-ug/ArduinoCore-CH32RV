# SoftSPI

日本語: [README.ja.md](README.ja.md)

Bit-banged SPI on any three pads, with the same API as [SPI](../SPI).

```cpp
#include <SoftSPI.h>

SoftSPI bus(PA1, PA2, PA4);      // SCK, MOSI, MISO
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
SoftSPI bus(PA1, PA2, PA4);
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

Measured on CH32V003 (16 KB flash), against the same sketch built with the
hardware `SPI`:

| | flash |
|---|---|
| hardware `SPI` | +1056 B |
| `SoftSPI` | +1376 B |

`SoftSPI` is the **larger** of the two: `digitalWrite()` is a call per edge
where the peripheral is one register write, and deriving from `HardwareSPI`
means the vtable keeps every override. 128 B of that total is
`setHalfPeriodUs()` pulling in `delayMicroseconds()`.

Both fit a 16 KB part with room to spare, and neither costs anything to a
sketch that does not include the header.
