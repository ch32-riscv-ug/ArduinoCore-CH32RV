# DualCoreV3F

[日本語](README.ja.md)

Run code on the CH32H417's second core, the V3F, while the sketch runs on the V5F.

## Hardware and connections

A CH32H417 board with **CPU Core = V5F** (the default). Connect a UART monitor to `Serial` (on CH32H417QEU6 PB14 TX / PB15 RX unless the sketch moves it with `Serial.setPins()`).

## Steps and expected results

Once a second the V5F prints how many primes the V3F has found and the last one; both numbers keep growing.

## API and processing flow

- `setup1()` / `loop1()`: when the sketch defines either, the core starts the V3F before `setup()` and runs them there, as arduino-pico does on its second core. The V3F's stack (4 KB, `CH32RV_V3F_STACK_SIZE`) is taken from the heap.
- `CH32RV.startV3F(fn, arg, stack, size)`: without `setup1()` / `loop1()`, hand the V3F one function at a time; `CH32RV.isV3FRunning()` says when it has returned. `CH32RV.isOnV3F()` tells the two cores apart.
- Share data through `volatile` variables with one writer each, or the A extension's atomics. The shared SRAM has no cache on either core.

## Adapting the example

A function on the V3F runs with interrupts on and may use a peripheral the V5F does not (`Serial1` and the like) with its interrupts; not `Serial` (the V5F's), the system services such as USB, or the heap (`String` and the like). `millis()`, `micros()`, `delay()` and `delayMicroseconds()` work (`delay()` there does not run USB and the other system services). The whole set of rules is in the [CH32RV README](../../README.md) under "H41x's two cores"; messages and a semaphore between the cores are in the example [DualCoreMessages](../DualCoreMessages/README.md). Both cores use the same instruction set (rv32imafc with B), so any function of the sketch can run on either. A function that never returns keeps the V3F until a reset: nothing stops it from outside yet.

## Related documentation

- [Sketch](DualCoreV3F.ino)
- [Basic example setup and index](../README.md)
- [The cores of the CH32H417](../../../../docs/h417-cores-design.ja.md)
