# QueueFromInterrupt

日本語: [README.ja.md](README.ja.md)

An interrupt hands values to `loop()` through a `Queue`. The ISR stores the press time and returns; `loop()` prints.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. `QueueFromInterrupt ready` at start.
2. Each press prints `PRESS ms=... queued=...`.
3. Press more than 8 times quickly: `loop()` only drains every 200 ms, the 8-place queue fills, and `DROPPED n` appears.

## Settings and limits

In the ISR a failed `trySend()` is counted, never waited on. A full queue refuses the new value and keeps the old ones (no overwrite). Contact bounce can turn one press into several.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[QueueFromInterrupt.ino](QueueFromInterrupt.ino)
