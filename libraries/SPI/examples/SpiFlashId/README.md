# SpiFlashId

[日本語](README.ja.md)

Read a three-byte JEDEC ID from an external SPI Flash that supports command `0x9F` in ordinary single-lane SPI mode. This example does not issue erase, program or write-enable commands.

## Connections and operation

Use a separate compatible Flash device, not Flash currently used to execute firmware. Check its datasheet for supply, I/O levels, pinout, command support and inactive levels of any extra control pins. Connect SCK, MOSI, MISO, `CS_PIN` (`SS` when defined, otherwise the example GPIO `PA4`), power and common GND. Leave no required HOLD/RESET or WP input floating. A QSPI pinout does not mean the chip is already in ordinary SPI mode.

Use the board's valid SCK/MISO/MOSI route, with a separate CS GPIO. Avoid conflicts with UART. On startup the sketch reads the ID; send `i` to repeat. Compare manufacturer/type/capacity bytes with the device's own documentation rather than an ID hardcoded in this guide. Unpadded hex output such as `1` means byte `01`.

## API and adaptation

`SPI.begin(SCK, MISO, MOSI)` reports whether the route can be opened. `SPISettings(1000000, MSBFIRST, SPI_MODE0)` selects this example's conservative requested rate, bit order and mode; confirm these suit the device. `beginTransaction()` configures the bus but does not select the chip. The sketch holds CS low for `0x9F` and three dummy transfers, then releases CS before `endTransaction()`.

Every transfer sends and receives simultaneously. Dummy output bytes provide clocks for the reply. SPI has no address ACK; `000000` and `FFFFFF` are suspicious readings, but neither other data nor successful startup alone proves a valid device. Check power, MISO, CS, wiring length and protocol when values are implausible.

When sharing the bus, keep every other device's CS inactive and use each device's settings in its own transaction. Do not treat `beginTransaction()` as a scheduler lock or call bus operations from an ISR. See [SPI](../../README.md) and [SPILoopback](../SPILoopback/README.md).

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
