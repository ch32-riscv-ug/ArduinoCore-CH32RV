# ShiftOut

[日本語](README.ja.md)

Send bytes to a shift register and activate its outputs in sequence.

## Hardware and connections

For a 74HC595, connect DS to `DATA_PIN`, SHCP to `CLOCK_PIN`, STCP to `LATCH_PIN`, OE to GND, and MR to supply. Provide power/common GND and resistors for output LEDs.

## Steps and expected results

The active LED moves through Q0–Q7. Compare bit order with output wiring.

## API and processing flow

`shiftOut(data, clock, MSBFIRST, value)` generates data and clock in software. Keep latch Low during transfer, then raise it to update outputs.

## Adapting the example

These GPIOs need not be SPI pins. Check input-high requirements at the IC supply voltage and both per-output and total current ratings.

## Related documentation

- [Sketch](ShiftOut.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
