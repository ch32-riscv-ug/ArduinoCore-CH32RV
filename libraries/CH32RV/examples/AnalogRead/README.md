# AnalogRead

[日本語](README.ja.md)

Observe raw analog input readings on UART.

## Hardware and connections

Connect a potentiometer between GND and a supply within the input range, with the wiper on `SENSOR` (`A1` here). Connect a UART monitor.

## Steps and expected results

Turn the potentiometer and check continuously changing raw values. A floating input can wander; also test with a defined voltage.

## API and processing flow

`analogRead(pin)` returns an integer at the configured resolution. `analogReadResolution(bits)` selects the result width. Analog aliases depend on the variant.

## Adapting the example

The voltage display assumes 12-bit readings and a 3.3 V reference, as its label states. Call `analogReadResolution(12)` before reading, or match the divisor to the chosen resolution. Check the reference supply too; this display is not a precision voltage measurement.

## Related documentation

- [Sketch](AnalogRead.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
