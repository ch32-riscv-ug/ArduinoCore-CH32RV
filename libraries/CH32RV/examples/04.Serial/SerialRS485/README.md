# SerialRS485

[日本語](README.ja.md)

Query an RS485 bus once a second through a transceiver (MAX485, SP3485 ...) whose driver enable the core switches
around each write, and print the replies on `Serial`.

## Hardware and connections

A 3.3 V RS485 transceiver. Its DI to `Serial1`'s TX, RO to `Serial1`'s RX, DE and /RE together to `DE_PIN` (A1 in the
sketch; any pad will do), A / B to the bus with its termination, common GND. A device on the bus that answers `?\n`
at 9600 8N1. `Serial` (115200) shows the replies.

## Steps and expected results

The board prints `RS485 ready`, then sends `?` every second and prints what came back within 200 ms, or `no reply`.
On a scope DE rises before the first start bit and falls right after the last stop bit.

`RS485 mode refused` means `setMode()` returned false: no RTS pin was named.

## API and processing flow

`Serial1.setPins(-1, -1, -1, DE_PIN)` names the RTS pin, which `setMode(UART_MODE_RS485_HALF_DUPLEX)` makes the driver
enable (as on an ESP32). No CH32 USART drives a DE pin itself: the core raises it in `write()` before the byte is handed
on and drops it in the transmit-complete interrupt, so it covers the whole frame without the sketch doing anything.
DE stays low after `end()`.

## Adapting the example

Change `DE_PIN`, the speed and the query. `setMode(UART_MODE_UART)` goes back to a plain UART and lets the DE pin go.
Hardware flow control is off in RS485 mode.

## Related documentation

- [Sketch](SerialRS485.ino)
- [Basic example setup and index](../../README.md)
