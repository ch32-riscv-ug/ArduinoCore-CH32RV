# PdmLevel

[日本語](README.ja.md)

A PDM microphone's level: the DFSDM clocks it from CKOUT at about 2 MHz, takes its data on DATIN1, and its sinc3 filter / 64 makes 31.25 kHz samples. Every 100 ms the sketch prints their RMS and peak with a bar.

## What you need

- A CH32H41x board
- A PDM microphone (a MEMS one: CLK, DATA, and L/R or SELECT to choose the edge)

## Wiring

| Microphone | Board |
|---|---|
| CLK | PE9 (DFSDM CKOUT) |
| DATA | PD6 (DFSDM DATIN1) |
| L/R (SELECT) | GND (data on the rising edge) |
| VDD / GND | 3.3 V / GND |

Any of the variant's `CH32RV_DFSDM_PADS` for CKOUT and DATIN0 / DATIN1 will do.

## What happens

It prints `clock 2000000 Hz, 31250 samples/s, full scale 262144`, then a line every 100 ms; the bar grows when you speak near the microphone.

## When it does not work

- `begin() failed`: a pin is not the DFSDM's for that role.
- The values stay at +-full scale: DATA held high or low (no microphone, or not powered).
- Nothing moves: with L/R tied high the microphone talks on the falling edge - call `mic.setFallingEdge(true)` before `begin()`.

## Before you run it

The output is on the hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` has the build profiles. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the [upload routes](../../../../docs/upload-routes.ja.md).
