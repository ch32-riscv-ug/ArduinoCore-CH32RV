# SoftSPILoopback

[日本語](README.ja.md)

Check software-generated SPI using a loopback.

## Hardware and connections

Jumper `MOSI_PIN` to `MISO_PIN` and connect a UART monitor. Choose GPIOs unused by other functions.

## Steps and expected results

Inspect each SPI mode result. An unconnected MISO floats and is not a valid communication test.

## API and processing flow

Declare `SoftSPI bus(SCK, MISO, MOSI)`, call `begin()`, then use transactions and `transfer()`. As with hardware SPI, the return is the simultaneously received value.

## Adapting the example

Software SPI generates signals on the CPU, so requested frequency need not match the waveform exactly. Measure timing where interrupts or concurrent work matter. Real slaves also require CS control.

## Related documentation

- [Sketch](SoftSPILoopback.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
