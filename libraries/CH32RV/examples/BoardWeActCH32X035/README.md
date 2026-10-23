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

### Board connections and shared pins

This template targets the WeAct CH32X035 Core Board with CH32X035F8U6. Use the schematic for your board revision.

| Function | Sketch pins | Notes |
|---|---|---|
| LED | PB12 | High turns it on |
| `Serial` | TX PA2, RX PA3 | USART2 |
| `Serial1` | TX PB0, RX PB1 | USART4 |
| SPI | SCK PA5, MISO PA6, MOSI PA7, CS PA4 | CS is an ordinary GPIO |
| ADC | A0 / PA0, A1 / PA1 | Match input voltage to the analog supply |
| PWM | PB11 | Keeps the UART, SPI and ADC pins free |
| USB data | D− PC16, D+ PC17 | Separate from the CC pins |
| USB PD | CC1 PC14, CC2 PC15 | Contract status is independent of CDC communication |
| Debug | PC18 / PC19 | Shared with one optional I2C route |

Only one optional I2C pair can be enabled: SDA PC18 / SCL PC19 replaces debug-pin use, or SDA PC17 / SCL PC16 replaces USB data. Uncomment `<Wire.h>`, the chosen pair and `Wire.begin()` together, after disabling the conflicting function. The package does not expose every USART route, so do not assume `Serial` must use USART1.

The DOWNLOAD key participates in the board's factory-upload entry. Follow the [upload routes (Japanese)](../../../../docs/upload-routes.ja.md) rather than treating it as an ordinary application button.

## Related documentation

- [Sketch](BoardWeActCH32X035.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
