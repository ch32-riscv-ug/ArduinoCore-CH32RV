# BoardWeActCH32X035

[日本語](README.ja.md)

A starting point combining the peripherals exposed by the WeAct CH32X035 Core Board, with explicit pin names.

## Hardware and connections

Use the target board and match UART/SPI wiring to the sketch pin table. USB CDC requires a data connection; PD tests require a suitable supply.

## Steps and expected results

Observe LED/PWM changes and ADC/PD status on `Serial` and connected `SerialUSB`. Remove unused features one at a time.

## API and processing flow

UART and SPI initialization specify pins. Read USB communication state and PD contract separately. `status(Print &out)` shares one output routine between UART and CDC.

## Adapting the example

Check the comments for pin conflicts. Enabling I2C requires removing the USB or debug-pin function it replaces. Compare schematics before using this on another X035 board.

## Related documentation

- [Sketch](BoardWeActCH32X035.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
