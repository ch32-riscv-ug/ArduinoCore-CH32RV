# AnalogContinuous

[日本語](README.ja.md)

arduino-esp32's continuous ADC API (`analogContinuous()`, `analogContinuousStart()`, `analogContinuousRead()`): the ADC converts `A0` 10,000 times a second by itself, and each frame of 100 conversions is averaged. As on an ESP32 the core declares it, so no include is needed.

## What happens

Every 10 ms a line such as `ADC_CONTINUOUS pin=64 raw=2047 mV=1650`: the average of the newest frame, raw (12 bits, `analogContinuousSetWidth(12)`) and in mV.

## API and what to watch when changing it

- `analogContinuous(pins, count, conversions_per_pin, sampling_freq_hz, callback)` sets it up: up to 16 ADC1 pins, the frame, the pace (conversions a second over all the pins: one round of the pins per tick of a timer the core takes, exact within the timer's divisions of its clock) and the function called from the interrupt as each frame fills. It fails when a pin is no ADC1 input or no trigger timer is free.
- `analogContinuousRead(&result, timeout_ms)` waits up to `timeout_ms` for a frame and gives each pin's average over the newest one. The mV are against the supply measured (against Vrefint) when `analogContinuous()` was called; 3300 mV where the chip has no Vrefint.
- The ADC has no attenuator: `analogContinuousSetAtten()` is accepted and does nothing. `analogContinuousDeinit()` frees the timer and gives the ADC back to `analogRead()`; `analogRead()` must not be used in between.
- For the samples themselves, a trigger on a pin's edge or a free-running ADC, see [CH32RVAdcStream](../../../CH32RVAdcStream/README.md).

## Before running

Output is the hardware `Serial` at 115200 baud 8N1. The generated `sketch.yaml` has the build profiles. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the [upload routes](../../../../docs/upload-routes.ja.md).
