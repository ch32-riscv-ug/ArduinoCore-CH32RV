# MessageBuffer

日本語: [README.ja.md](README.ja.md)

Messages of different lengths, each kept whole: `trySend()` stores all or nothing, `tryReceive()` returns one whole message.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. Each press queues `hi` or `a longer message` alternately; they come out as `MESSAGE ...` every 2 s.
2. Press many times within 2 s: the 48 bytes fill up and the rest is `REFUSED n`.

## Settings and limits

A message costs its length + 2 bytes. With a too-small buffer `tryReceive()` returns 0 and keeps the message; `nextLength()` says how much room it needs.

## API usage and adaptation

`trySend(data, bytes)` stores a complete message or nothing. `tryReceive(dst, capacity)` returns its received length. Leave room for a terminator when displaying it as text.

An oversized first message prevents access to later messages, even if they are shorter. Inspect `nextLength()` and match maximum stored length to receive capacity. Protect read/reset of auxiliary refusal counters if exact totals matter.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[MessageBuffer.ino](MessageBuffer.ino)
