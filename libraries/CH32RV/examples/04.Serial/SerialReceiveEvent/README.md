# SerialReceiveEvent

[日本語](README.ja.md)

Receive a whole message at once when the line goes quiet (ESP32's `onReceive()`), and be told about receive errors
(`onReceiveError()`), instead of polling `available()`.

## Hardware and connections

A USB-UART adapter on `Serial`: adapter RX to board TX, TX to RX, common GND.

## Steps and expected results

Open at 115200 8N1 and send `hello` without a line ending. The board prints `Got 5 bytes: hello` once - after three
frames of silence, not per byte. Send at another baud rate: `Receive error mask 0x10` (bit 4, `UART_FRAME_ERROR`) or
other bits appear.

## API and processing flow

`setRxTimeout(3)` makes three frames of silence a timeout; `onReceive(onMessage, true)` calls `onMessage()` only then
(with `false` it is also called when the buffer holds `setRxFIFOFull()` bytes, half the buffer by default). Both
callbacks run after `loop()` (and in `deliverEvents()`), never in the interrupt, so they may read and print. The
first frame of silence is the USART's IDLE flag; the rest are counted when the event is delivered, so a long
`loop()` makes them late, not early.

## Adapting the example

Messages longer than the receive buffer (63 bytes by default) overflow it before the timeout: read in the callback
with `onlyOnTimeout` false, or raise `CH32RV_SERIAL_RX_BUFFER_SIZE`. `hardwareSerial_error_t` lists the error kinds;
`UART_NOISE_ERROR` is this core's own (the byte was kept).

## Related documentation

- [Sketch](SerialReceiveEvent.ino)
- [Basic example setup and index](../../README.md)
