# HelloSDI

[日本語](README.ja.md)

Print text through the debug connection without UART.

## Hardware and connections

Use a supported probe and debug wiring; no MCU UART wiring is needed.

## Steps and expected results

Start a monitor such as `ch32rv monitor --source sdi --chip CH32V003`, adapting the chip, and inspect startup text and uptime.

## API and processing flow

Call `SerialSDI.begin()`, then `print()`/`println()`. The baud argument does not set a UART clock.

## Adapting the example

The host must collect SDI output too. Check [debug output (Japanese)](../../../../docs/debug-output.ja.md) for probe/series requirements. Do not share these debug registers with SerialDMDATA or SerialDMSeq in the same sketch.

## Related documentation

- [Sketch](HelloSDI.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
