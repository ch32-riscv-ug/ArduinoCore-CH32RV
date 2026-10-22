# TryLock

日本語: [README.ja.md](README.ja.md)

`loop()` fills a buffer in several steps and an ISR must not touch it meanwhile. The ISR cannot wait, so it tries the lock and skips when it is taken.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. `DONE isr_writes=... isr_skips=...` every 400 ms.
2. A press during the fill raises `isr_skips`; otherwise `isr_writes`.

## Settings and limits

`loop()` cannot run until the ISR returns, so an ISR waiting for loop()'s lock would never return. Hold a lock from one loop() to the next only on purpose, as here.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[TryLock.ino](TryLock.ino)
