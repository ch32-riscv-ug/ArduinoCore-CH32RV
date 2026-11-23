# LowPowerTick

[日本語](README.ja.md)

Sleeps in `lightSleep()` (Stop) and lets the low-power timer (LPTIM) wake the chip. The LPTIM counts LSI, so it keeps running while the other clocks are stopped.

## Wiring

None.

## What happens

After upload it prints something like `LPTIM at 1250 Hz` (from LSI's nominal rate; the real LSI is off by tens of percent), then about four times a second `woke, tick 1`, `woke, tick 2`, ...

## When it does not work

- `LPTIM begin() failed`: the series has no LPTIM (CH32L103 / M103 and CH32H41x have one).
- The interval is far from 250 ms: that is LSI's spread. For an exact one use `CLOCK_LSE` (LSE chosen in the RTC Clock menu, on a board with the crystal).

## Before you run it

The output is on the hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` has the build profiles. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the [upload routes](../../../../docs/upload-routes.ja.md).
