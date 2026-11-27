# WeActCH32X035OledSH1106

[日本語](README.ja.md)

An SH1106 128x64 I2C OLED on the WeAct CH32X035 Core Board's PC18 (SDA) / PC19 (SCL), with text, a counter and
a bar drawn by the [U8g2](https://github.com/olikraus/u8g2) library.

## Hardware and connections

| SH1106 module | WeAct CH32X035 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | PC18 |
| SCL | PC19 |

Most modules carry their own pull-ups (4.7-10 kOhm); without them, add them on SDA and SCL. The address is 0x3C
(0x3D with the module's jumper moved; the sketch's `setI2CAddress()`).

U8g2 is not bundled with the core: install "U8g2" from the Library Manager (checked with 2.36.19; a build from the
sketch.yaml profile installs that version by itself).

## Uploading and operation

This board has I2C only on the debug pads PC18 / PC19 (or the USB pads PC16 / PC17). The sketch makes PC18 / PC19
ordinary pads with `CH32RV.releaseDebugPins()`, so no WCH-Link or OEP probe can attach until the next reset.

- **Upload over USB:** the sketch keeps a USB device (`SerialUSB`) up, so the next upload goes to the board's
  `ch32rv-app://` port or serial port. The very first one, as for [WeActCH32X035](../WeActCH32X035/README.md):
  pick `WAIT_FOR_BOOT`, press Upload, then plug the USB in while holding the DOWNLOAD key.
- **What shows:** `Hello, SH1106!`, `WeAct CH32X035` and `uptime <seconds> s`, and the bar at the bottom grows over
  10 seconds and starts again.

## Adapting the example

- Keep `SerialUSB.begin()`: release the debug pads with no USB device running and nothing is left to upload the
  next sketch through but the DOWNLOAD key.
- An SSD1306 module: `U8G2_SSD1306_128X64_NONAME_1_HW_I2C`.
- For other boards with a pin choice, see Wire's [I2COledSH1106U8g2](../../../../Wire/examples/I2COledSH1106U8g2/README.md);
  without a display library, [I2COledSH1106](../../../../Wire/examples/I2COledSH1106/README.md).

## Related documentation

- [Sketch](WeActCH32X035OledSH1106.ino)
- [WeActCH32X035](../WeActCH32X035/README.md): the board's small start and its pins
- [The debug pads (`releaseDebugPins()`)](../../../README.md)
