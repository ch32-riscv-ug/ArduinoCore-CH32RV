# PrintFormatting

[日本語](README.ja.md)

Compare integer, floating-point and string formatting with raw byte output.

## Hardware and connections

Connect a UART monitor and ensure the formatting routines fit the target memory.

## Steps and expected results

At startup, compare integer bases, decimal places, `dtostrf()` output, and the different results of printing and writing 65.

## API and processing flow

For integers, `print(value, HEX)` selects a base; for floating point, the second argument selects decimal places. `write()` does not format text. `dtostrf()` writes into a caller-provided array.

## Adapting the example

Display digits do not establish measurement accuracy. Allow room for sign, decimal point and terminator. Repeated `String` concatenation also requires attention to heap use.

## Related documentation

- [Sketch](PrintFormatting.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
