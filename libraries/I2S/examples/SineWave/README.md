# SineWave

[日本語](README.ja.md)

Plays a 441 Hz sine on an I2S DAC: 16-bit stereo at 44.1 kHz, the same sound on both channels.

## What you need

- A board of a series with I2S2 (CH32V30x, CH32V4x7, CH32H41x)
- An I2S DAC or amplifier (MAX98357A, PCM5102A, UDA1334A, ...) and a speaker or earphones

## Wiring

| Board | DAC |
|---|---|
| BCLK (PB13) | BCLK / BCK |
| WS (PB12) | LRC / LRCK / WS |
| DOUT (PB15) | DIN |
| 3.3 V, GND | VIN, GND |

CH32H416 bonds no PB12-PB15: there BCLK is PD3, WS PB4, DOUT PC3. Any pads of I2S2 will do: the variant lists them in `CH32RV_I2S2_PADS` (I2S3's in `CH32RV_I2S3_PADS`). On H41x any combination works, on V30x / V4x7 only the pads of one route.

## What happens

After upload it prints the rate it got, such as `I2S at 44102 Hz (asked 44100)`, and a 441 Hz tone plays on. Every 5 seconds it prints `underruns 0`.

## When it does not work

- `error 2`: the pins are not an I2S's on this series.
- `error 1`: the divider cannot make the rate (16-bit slower than SYSCLK / 16352: below 24.5 kHz at H41x's 400 MHz).
- No sound: check the DAC's supply and that WS and BCLK are not swapped. A MAX98357A is shut down while its SD pin is low.
- `underruns` grows: something in `loop()` takes long enough for the DMA buffer to run dry.

## Before you run it

The output is on the hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` has the build profiles. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the [upload routes](../../../../docs/upload-routes.ja.md).
