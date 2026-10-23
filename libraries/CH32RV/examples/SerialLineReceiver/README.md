# SerialLineReceiver

[日本語](README.ja.md)

Receive a text line incrementally in a fixed buffer while allowing other work between incoming characters. Compare this with [SerialEcho](../SerialEcho/README.md), which waits in a Stream line-reading function.

## Operation

Set the terminal to LF or CRLF. Send `hello`: the reply is `Line: hello`. Send several lines together, an empty line, and a line with surrounding spaces. Spaces are retained; only CR immediately before LF is removed. CR alone does not finish a line. This is a text example; use [SerialPacketReceiver](../SerialPacketReceiver/README.md) for arbitrary binary data.

The example holds up to 31 bytes before LF, including CR if present. A longer input is discarded through LF and reported once. Its trailing characters must not become a second accepted line. Send a partial line, pause for more than one second, then continue: timeout discards the partial input, and later bytes start a new line.

## API and adaptation

`available()` checks buffered bytes and `read()` removes one. No read waits for a future character. Each loop handles at most 16 bytes, with unsigned `millis()` subtraction for the idle timeout. The array leaves one byte for `\0`; a terminator is written only after a valid line completes.

The timeout measures inactivity between processed bytes, not total line duration. Input is bytes, so multibyte text consumes multiple positions. UART hardware buffers can still overflow if foreground work is too slow. The receive budget does not make `Serial.print()` nonblocking: for continuous high-rate traffic, shorten or queue replies and check `availableForWrite()` before sending. Add your own short work after the receive loop.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
