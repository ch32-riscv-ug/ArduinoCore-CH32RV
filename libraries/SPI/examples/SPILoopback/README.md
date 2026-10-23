# SPILoopback

[日本語](README.ja.md)

Check hardware SPI transmission/reception with a MOSI-to-MISO loopback.

## Hardware and connections

Jumper `MOSI_PIN` to `MISO_PIN`. Connect a UART monitor and adapt bus/UART pins to the board.

## Steps and expected results

Confirm sent and received bytes match with `ok`. This does not test an external SPI slave.

## API and processing flow

`SPI.begin(SCK, MISO, MOSI, -1)` opens the bus. Between `beginTransaction(SPISettings(clockHz, order, mode))` and `endTransaction()`, `transfer(byte)` returns the simultaneously received byte.

## Adapting the example

Note SCK–MISO–MOSI argument order. For a real peripheral, choose its mode/clock and add CS handling. Loopback does not verify the peripheral protocol or CS.

## Related documentation

- [Sketch](SPILoopback.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
