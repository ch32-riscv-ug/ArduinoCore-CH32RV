# UIAPduinoProMicro

[日本語](README.ja.md)

Blink the UIAPduino Pro Micro's LED by its printed pin number (2 = PC0). Upload over USB alone and it runs.

## Hardware and connections

A UIAPduino Pro Micro CH32V003 V1.4 and a USB cable. Select the board "UIAPduino Pro Micro CH32V003 V1.4"
and the board's USB port (the HID bootloader), then upload.

## Steps and expected results

The on-board LED flashes twice, briefly, then pauses, over and over.

## About this board's USB

The board's USB belongs to the bootloader that uploads sketches. While a sketch runs, the USB does
nothing (CH32V003 has no USB peripheral), so `Serial` output cannot be seen over USB alone. To try other
features, add one of these:

- **UART:** wire a USB-UART adapter to TX = 15 (PD5), RX = 16 (PD6) and GND, and use `Serial.begin(115200)`.
- **The debug wire:** a WCH-LinkE on SWIO (11 = PD1) and GND reads `SerialSDI` or `SerialDMSeq` with no
  UART wiring ([debug output, Japanese](../../../../../docs/debug-output.ja.md)).

## Board pins

The printed numbers (0 to 17), `D0` to `D17` and the pad names (`PC0`, ...) all reach the same pads.

| Function | Printed number / pad |
|---|---|
| LED | 2 / PC0, active High |
| UART | TX 15 / PD5, RX 16 / PD6 |
| I2C | SDA 3 / PC1, SCL 4 / PC2 |
| SPI | SCK 7 / PC5, MISO 9 / PC7, MOSI 8 / PC6 |
| ADC | A0 = 1 / PA2, A1 = 0 / PA1, A2 = 6 / PC4 |
| A PWM pad | 12 / PD2 |
| Kept by the board | USB 13 / PD3 and 14 / PD4 (the bootloader), SWIO 11 / PD1, RESET 17 / PD7 |

Leave the pads the board keeps alone, so it can still be uploaded to and recovered. The maker's SPI NSS
alternatives (PC1 / PC0) are I2C's SDA and the LED; use a free GPIO such as 5 / PC3 for SPI's CS.

## Related documentation

- [Sketch](UIAPduinoProMicro.ino)
- [Basic example setup and index](../../README.md)
- [Example guide](../../../../../docs/examples.md)
- [Sketch composition guide (Japanese)](../../../../../docs/sketch-guide.ja.md)
