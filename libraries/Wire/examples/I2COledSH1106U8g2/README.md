# I2COledSH1106U8g2

[日本語](README.ja.md)

Text, a counter and a bar on an SH1106 128x64 OLED over I2C with the [U8g2](https://github.com/olikraus/u8g2)
library: fonts and drawing, closer to a real display. The version without a display library is
[I2COledSH1106](../I2COledSH1106/README.md).

## Hardware and connections

An SH1106 I2C module: VCC to 3.3 V, GND, SDA and SCL to the sketch's pins (pull-ups and address as in
[I2COledSH1106](../I2COledSH1106/README.md)).

U8g2 is not bundled with the core: install "U8g2" from the Library Manager. The example's sketch.yaml names the
version it was checked with (2.36.19), so a profile build installs that version by itself.

## Steps and expected results

`Hello, SH1106!`, `CH32RV + U8g2` and `uptime <seconds> s` show, and the bar at the bottom fills in 10 seconds
and starts again.

## API and processing flow

On this core (anything but ESP32) U8g2 calls `Wire.begin()` with no pins, so the sketch chooses them first with
`Wire.setPins(SDA, SCL)` (Wire then opens those). The address goes shifted: `setI2CAddress(0x3C << 1)`.

Page mode (`_1_`) draws one page at a time from a 128-byte buffer, so the same picture is drawn on every pass of
the `firstPage()` / `nextPage()` loop. It fits V003 (2 KB of RAM): 13.2 KB of flash and 464 B of RAM there.

## Adapting the example

- With RAM to spare, `U8G2_SH1106_128X64_NONAME_F_HW_I2C` keeps the whole 1 KB screen: draw after
  `clearBuffer()` and send it at once with `sendBuffer()`.
- An SSD1306 module: `U8G2_SSD1306_128X64_NONAME_1_HW_I2C`.
- **On the WeAct CH32X035 (F8U6)** with PC18 / PC19, switch to the pins in the comment and set `USE_DEBUG_PADS`
  to 1, as in [I2COledSH1106](../I2COledSH1106/README.md) (a USB device is kept up, so the next upload goes over
  USB).

## Related documentation

- [Sketch](I2COledSH1106U8g2.ino)
- [I2COledSH1106](../I2COledSH1106/README.md): the version without a display library
- [I2CScanner](../I2CScanner/README.md): find the module's address
- [Library API](../../README.md)
