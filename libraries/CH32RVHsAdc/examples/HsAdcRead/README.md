# HsAdcRead

[日本語](README.ja.md)

Perform individual high-speed ADC conversions and print counts and estimated voltage.

## Hardware and connections

Connect an in-range input to PC0 and common GND on the target board. The sketch raises VIO18 to 3.3 V; first check that every component on that rail tolerates it.

## Steps and expected results

Inspect `HSADC at ... samples/s` and PC0 readings on UART. On initialization failure, check the input pin and target part.

## API and processing flow

`hsadc.begin(pin, samplesPerSecond)` initializes conversion; `sampleRate()` reports the selected rate. `read()` performs an individual conversion.

## Adapting the example

Input limits follow the pin supply. Voltage conversion depends on its reference, so inspect raw counts as well. Do not combine single reads with active continuous capture; see the [library README](../../README.md).

## Related documentation

- [Sketch](HsAdcRead.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
