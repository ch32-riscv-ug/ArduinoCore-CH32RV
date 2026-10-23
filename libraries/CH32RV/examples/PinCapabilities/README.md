# PinCapabilities

[日本語](README.ja.md)

Inspect variant-defined pins and their ADC mappings.

## Hardware and connections

Connect a UART monitor; no other wiring is needed.

## Steps and expected results

At startup the sketch lists aliases such as `A1`, bus pins, and valid pads. Reset to print again.

## API and processing flow

`digitalPinIsValid()` checks validity and `digitalPinHasADC()` checks ADC capability. Use pad names such as `PA1` or variant aliases instead of assuming consecutive pin numbers.

## Adapting the example

This describes the variant used for compilation, not electrical discovery. Check the PCB schematic to see whether pads reach headers or are connected to other components.

## Related documentation

- [Sketch](PinCapabilities.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
