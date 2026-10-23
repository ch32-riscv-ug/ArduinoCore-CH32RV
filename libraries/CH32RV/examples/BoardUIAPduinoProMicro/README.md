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

### Board pin assignments

The template is for the UIAPduino Pro Micro CH32V003 V1.4. The aliases below describe this board, not a Generic V003 PCB.

| Function | Pad / board label |
|---|---|
| LED | PC0 / D2, active High |
| UART | TX PD5 / D15, RX PD6 / D16 |
| I2C | SDA PC1 / D3, SCL PC2 / D4 |
| SPI | SCK PC5 / D7, MISO PC7 / D9, MOSI PC6 / D8 |
| GPIO CS | PC3 / D5 |
| ADC | A0 PA2 / D1, A1 PA1 / D0, A2 PC4 / D6 |
| PWM | PD2 / D12 |
| Debug / reset | SWIO PD1 / RESET PD7 |

The maker's NSS alternatives PC1 and PC0 conflict with I2C SDA and the LED respectively. To use either as CS, disable the function it replaces. The sketch drives CS as a GPIO; it does not need to match hardware NSS. Its USB bootloader uses PD3/PD4; that wiring does not establish application USB Device support.

## Related documentation

- [Sketch](BoardUIAPduinoProMicro.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
