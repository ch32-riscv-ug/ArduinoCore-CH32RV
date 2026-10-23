# BoardUIAPduinoProMicro

[日本語](README.ja.md)

Initialize UART, I2C, SPI, ADC and PWM using the UIAPduino Pro Micro board pin assignments.

## Hardware and connections

Use the target board and a UART monitor. Provide suitable I2C pull-ups and common GND; use the stated GPIO for SPI CS.

## Steps and expected results

Observe LED/PWM and UART status. Connect MOSI to MISO when testing SPI loopback.

## API and processing flow

`Wire.begin(SDA, SCL)` and `SPI.begin(SCK, MISO, MOSI, -1)` have different argument orders. CS is explicitly driven as a GPIO.

## Adapting the example

The schematic/pinout NSS label need not be the sketch CS. Check I2C/LED conflicts before changing CS. Distinguish the board USB upload mechanism from USB Device functionality available to an application sketch.

## Related documentation

- [Sketch](BoardUIAPduinoProMicro.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
