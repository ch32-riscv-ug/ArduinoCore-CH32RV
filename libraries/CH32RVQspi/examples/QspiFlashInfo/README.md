# QspiFlashInfo

[日本語](README.ja.md)

Reads a QSPI flash's (W25Q, GD25Q, ...) JEDEC ID, then its first 16 bytes with the one-, two- and four-line read commands.

## What you need

- A board of a series with QSPI (CH32V205, CH32H41x)
- A QSPI flash (one whose WP / HOLD pins serve as IO2 / IO3 for quad)

## Wiring

| Board (V205) | Board (H41x) | Flash |
|---|---|---|
| PA12 | PB2 | CLK |
| PB0 | PB6 | /CS |
| PA2 | PD11 | DI (IO0) |
| PA3 | PD12 | DO (IO1) |
| PA0 | PD14 | /WP (IO2) |
| PA1 | PD13 | /HOLD (IO3) |

CH32H416 bonds QSPI2's pads only: SCK PF6, /CS PF7, IO0-3 PF8, PF9, PF10, PE15. Any pads of the variant's `CH32RV_QSPIn_PADS` will do.

## What happens

It prints the rate it got, such as `QSPI at 9600000 Hz`, and `JEDEC ID EF 40 17` (a W25Q64). The three lines of 16 bytes after it read the same address with three commands, so they match.

## When it does not work

- `begin() failed`: the pins are not a QSPI's on this series.
- The ID is `FF FF FF` or `00 00 00`: check the wiring, the supply (3.3 V) and /CS.
- Only the quad line differs: the flash's QE bit (status register 2) is not set.

## Before you run it

The output is on the hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` has the build profiles. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the [upload routes](../../../../docs/upload-routes.ja.md).
