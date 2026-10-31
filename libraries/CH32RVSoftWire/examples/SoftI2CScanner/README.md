# SoftI2CScanner

[日本語](README.ja.md)

Scan I2C addresses using software I2C on chosen GPIOs.

## Hardware and connections

Connect `SDA_PIN`, `SCL_PIN`, common GND and signal pull-ups. Avoid GPIOs driven by other functions and connect a UART monitor.

## Steps and expected results

Repeated scans print responding addresses. If none are found with timeouts, check pull-ups and signals held Low.

## API and processing flow

Declare `SoftWire bus(SDA, SCL)`, then call `begin()` and `setClock(hz)`. Use `endTransmission()` for ACK status and `getWireTimeoutFlag()` for timeouts.

## Adapting the example

Clear the checked flag with `clearWireTimeoutFlag()`. Software cannot replace required open-drain pull-ups. Do not assume address probing is harmless for every attached device.

## Related documentation

- [Sketch](SoftI2CScanner.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
