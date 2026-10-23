# SerialEcho

[日本語](README.ja.md)

Receive a line over hardware UART and echo it with surrounding whitespace removed.

## Hardware and connections

Connect board TX to adapter RX, board RX to adapter TX, and common GND. Match I/O voltage and set `RX_PIN`/`TX_PIN` to your wiring.

## Steps and expected results

Open at 115200 baud, 8N1, and send a line including LF. After the startup message, the board replies `you said: ...`.

## API and processing flow

`available()` checks for received bytes. `readStringUntil('\n')` waits for the delimiter or timeout and returns a `String` without the delimiter. `trim()` also removes surrounding whitespace.

## Adapting the example

The first received byte does not establish a complete line. For responsive processing or binary forwarding, accumulate `read()` results in your own array instead. This is a UART example, not USB CDC.

## Related documentation

- [Sketch](SerialEcho.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
