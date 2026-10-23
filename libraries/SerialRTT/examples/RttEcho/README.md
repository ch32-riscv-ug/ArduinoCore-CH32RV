# RttEcho

[日本語](README.ja.md)

Echo uppercase text over RTT while continuing LED blinking.

## Hardware and connections

Use a compatible probe and debug connection. Blinking requires a board-appropriate `LED_BUILTIN` definition.

## Steps and expected results

Send text from the RTT monitor and inspect uppercase replies. Echo works without an LED definition too.

## API and processing flow

Read only available bytes and return them with `write()`. The LED uses `millis()` instead of blocking for input.

## Adapting the example

Use a monitor supporting input transmission. Even without polling waits, transmit storage is finite; check write results when adapting for continuous data.

## Related documentation

- [Sketch](RttEcho.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
