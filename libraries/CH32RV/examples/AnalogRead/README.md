# AnalogRead

[日本語](README.ja.md)

Observe raw analog input readings, and the voltage in mV, on UART.

## Hardware and connections

Connect a potentiometer between GND and a supply within the input range, with the wiper on `SENSOR` (`A1` here). Connect a UART monitor.

## Steps and expected results

Turn the potentiometer and check continuously changing raw values. A floating input can wander; also test with a defined voltage.

## API and processing flow

`analogRead(pin)` returns an integer at the configured resolution. `analogReadResolution(bits)` selects the result width. `analogReadMilliVolts(pin)` (arduino-esp32's name) returns the voltage: the ADC's reference is the supply, which the chip measures against its internal reference just before each reading, so it does not assume 3.3 V. Analog aliases depend on the variant.

## Adapting the example

The mV value is within the internal reference's spread plus the converter's error, a few percent; it is not a precision measurement (the CH32RV library README says what has been measured). CH32M030 has no internal reference in the data and assumes a 3.3 V supply.

## Related documentation

- [Sketch](AnalogRead.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
- [Sketch composition guide (Japanese)](../../../../docs/sketch-guide.ja.md)
