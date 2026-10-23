# I2CScanner

[日本語](README.ja.md)

Find I2C addresses that acknowledge.

## Hardware and connections

Connect SDA, SCL and common GND. Both signals need pull-ups to a suitable voltage; inspect existing module pull-ups too. Connect a UART monitor.

## Steps and expected results

The sketch scans once at startup and prints responding addresses. On a bus timeout, check wiring and pull-ups first. Reset to repeat.

## API and processing flow

`Wire.begin(SDA, SCL)` opens the bus. `beginTransmission(address)` followed by `endTransmission()` checks the result; zero means ACK. Pass seven-bit addresses.

## Adapting the example

An ACK does not identify or fully validate a device. Follow up with device-specific register access. Do not pass a datasheet address containing the read/write bit without converting it.

## Related documentation

- [Sketch](I2CScanner.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
