# I2COledSH1106

[日本語](README.ja.md)

Show text and a counter on an SH1106 128x64 OLED over I2C, with Wire alone (no display library).

## Hardware and connections

An SH1106 I2C module (often 1.3"): VCC to 3.3 V, GND, SDA and SCL to the sketch's pins. Most modules carry
their own pull-ups (4.7-10 kOhm); without them, add them on SDA and SCL. The address is usually 0x3C, 0x3D
with the module's address jumper moved (the sketch's `OLED_ADDR`).

## Steps and expected results

At start the first row shows `Hello, SH1106!`, the second `CH32RV Arduino`, the third the pins in use (by
name, as `SDA PC18  SCL PC19`), and the fourth row's `uptime` advances every 0.5 s. If nothing shows, check
the wiring, power and address, and see with I2CScanner whether 0x3C or 0x3D answers.

## API and processing flow

SH1106 tells commands (0x00) from display data (0x40) by a leading control byte. The sketch sends the
initialisation commands, then writes 5x7 glyphs, 6 pixels a character, at a page (an 8-pixel row) and a
column. It keeps no frame buffer (1 KB), so it fits a 2 KB part such as V003. SH1106's RAM is 132 columns
wide and a 128-pixel panel starts at column 2 (`COLUMN_OFFSET`). Data goes 16 bytes at a time, within Wire's
32-byte buffer.

## Adapting the example

- An SSD1306 128x64 module takes nearly the same commands, with `COLUMN_OFFSET` 0 (and 0x8D 0x14 instead
  of the DC-DC's 0xAD 0x8B).
- Upside down? Change 0xA1 / 0xC8 in the initialisation to 0xA0 / 0xC0.
- **On the WeAct CH32X035 (F8U6)** I2C is only on the debug pads PC18 / PC19 (or the USB pads PC16 / PC17).
  For PC18 / PC19, switch to the pins in the sketch's comment and set `USE_DEBUG_PADS` to 1:
  `CH32RV.releaseDebugPins()` makes the debugger let go of them, and no WCH-Link or OEP probe can attach
  until the next reset. The sketch keeps a USB device (`SerialUSB`) up, so the next upload goes over the
  board's USB (its `ch32rv-app://` port or serial port).

## Related documentation

- [Sketch](I2COledSH1106.ino)
- [I2CScanner](../I2CScanner/README.md): find the module's address
- [Library API](../../README.md)
- [The debug pads (`releaseDebugPins()`)](../../../CH32RV/README.md)
