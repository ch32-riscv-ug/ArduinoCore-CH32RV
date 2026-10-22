# NotifyBits

日本語: [README.ja.md](README.ja.md)

Several kinds of event in one object: each ISR raises a bit, `loop()` asks for any or all of them with `tryWaitBits()`.

## Hardware and connections

Push buttons from `BUTTON_A` (default `PA1`) and `BUTTON_B` (default `PA2`) to GND. A 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. `WAITING A B` (the ones not pressed yet) every 500 ms.
2. Press both A and B in any order: `BOTH`, then back to `WAITING A B`.

## Settings and limits

`waitAll=true` takes the bits only when all are up; `clearOnExit=false` looks without clearing. Raising a bit twice before it is taken counts once.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[NotifyBits.ino](NotifyBits.ino)
