# I2CScanner

[日本語](README.ja.md)

Find I2C addresses that acknowledge.

## Hardware and connections

Connect SDA, SCL and common GND. Both signals need pull-ups to a suitable voltage; inspect existing module pull-ups too. Connect a UART monitor.

## Steps and expected results

The sketch scans every 5 seconds and prints the pins it uses (by name, as in `SDA=PC18 SCL=PC19`) and the addresses that answer. `bus timed out` (`endTransmission()` returning 5) means SCL or SDA never went high: no pull-ups, a device holding a line low, or pins that are not actually I2C. `these pins cannot carry I2C here` means the pair is no I2C route of this chip, or a debug pad not yet released.

## Using the debug pads

To use debug pads such as X035's PC18 / PC19 for I2C, set the sketch's `USE_DEBUG_PADS` to 1 (it calls
`CH32RV.releaseDebugPins()`). Until the next reset the debugger lets go of them, and no WCH-Link or OEP
probe can attach meanwhile: upload over USB (on the WeAct X035, its `ch32rv-app://` port or serial port).
Make `Console` `SerialUSB`: a sketch with no USB device leaves no way to upload the next one but the board's BOOT / DOWNLOAD key (it happened on the WeAct X035).

## API and processing flow

`Wire.begin(SDA, SCL)` opens the bus. `beginTransmission(address)` followed by `endTransmission()` checks the result; zero means ACK. Pass seven-bit addresses.

## Adapting the example

An ACK does not identify or fully validate a device. Follow up with device-specific register access. Do not pass a datasheet address containing the read/write bit without converting it.

## Related documentation

- [Sketch](I2CScanner.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
