# RingBuffer

日本語: [README.ja.md](README.ja.md)

A fixed-size byte stream: characters typed on the UART are collected in a `Ring` and handled per line. `write()` takes what fits.

## Hardware and connections

A 115200-baud 8N1 UART monitor on RX/TX, sending LF or CRLF line endings.

## Steps and expected results

1. Send a line: `LINE bytes=n text=...`.
2. Send more than 64 characters: the excess is dropped and `OVERFLOW dropped=n` appears.

## Settings and limits

Only `loop()` uses this ring, so it is `LoopOnly` (no masking). Keep the default `IsrSafe` when an ISR shares it.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[RingBuffer.ino](RingBuffer.ino)
