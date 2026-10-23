# BlockPool

日本語: [README.ja.md](README.ja.md)

Records too big to copy: the ISR borrows a block from a pool, fills it and sends the pointer through a `Queue`; `loop()` uses it and returns it.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. A press shows `RECORD ms=... us=... free=n`.
2. Press 5 times within a second: the 4 blocks run out and `NO_BLOCK n` appears.

## Settings and limits

`release()` refuses a pointer that is not one of its blocks or is already back (a double release). A queue as long as the pool can never be full on send.

## API usage and adaptation

`pool.tryAllocate()` returns `nullptr` when no block is free. Fill an allocated block, queue its pointer, then have the consumer call `pool.release()` after use. Do not access it after release.

If changing queue sizes or producers, return the block on `trySend()` failure too. Protect read/reset of auxiliary failure counters together when exact statistics matter.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[BlockPool.ino](BlockPool.ino)
