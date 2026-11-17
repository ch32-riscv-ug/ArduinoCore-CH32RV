# DualCoreMessages

[日本語](README.ja.md)

The CH32H417's two cores talk: the V5F sends numbers to the V3F (IPC), the V3F tests them for primes and answers, and a hardware semaphore (HSEM) guards the totals the two share.

## Hardware and connections

A CH32H417 board with **CPU Core = V5F** (the default).

- A UART monitor on `Serial` (the V5F's; on CH32H417QEU6 PB14 TX / PB15 RX).
- Optional: a second monitor on `Serial1` (the V3F's; on CH32H417QEU6 PD5 TX / PD6 RX). PD5 / PD6 are VIO18 pins: for a 3.3 V monitor set the VIO18 Supply menu to 3.3 V.

## Steps and expected results

- `Serial` shows a line every half second such as `V5F: 1000003 is prime (1 of 1 tested are prime)`. The number goes up by 2, and the totals grow.
- `Serial1` shows the V3F's `V3F: 1000003 is prime`.
- With no answer from the V3F within a second the V5F prints `V5F: no answer from the V3F`; when the V5F has not taken the last answer within 100 ms the V3F prints `V3F: the V5F did not take the last answer` on `Serial1`.

## API and processing flow

- `CH32RV.ipcOnReceive(cb)`: `cb(uint32_t)` runs in an interrupt for each message to this core - the V3F's when called in `setup1()`, the V5F's in `setup()`.
- `CH32RV.ipcSend(msg, ms)`: one 32-bit message to the other core; waits up to `ms` for the last one to be taken and returns whether it went in.
- `CH32RV.hsemTake(ch)` / `CH32RV.hsemRelease(ch)`: take and give back an HSEM channel; only the core that took it can release it. The two words of `totals` written between them are always seen together by the other core.
- The V3F hands the number from its interrupt to `loop1()` and does the slow work outside the interrupt.
- `Serial1` is begun in `setup1()` (on the V3F), so its interrupt is taken on the V3F too.

## Adapting the example

A peripheral is used from one core only (`Serial` the V5F, `Serial1` the V3F). No heap on the V3F (`String`, `new`). In an interrupt handler pass 0 as `ipcSend()`'s wait and use `hsemTryTake()`, never `hsemTake()`. Built with CPU Core = V3F the V5F stays asleep: `ipcSend()` returns false and no answer comes. The whole set of rules is in the [CH32RV README](../../README.md) under "H41x's two cores".

## Related documentation

- [Sketch](DualCoreMessages.ino)
- [Basic example setup and index](../README.md)
- [DualCoreV3F](../DualCoreV3F/README.md)
- [The cores of the CH32H417](../../../../docs/h417-cores-design.ja.md)
