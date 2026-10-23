# TimerResource

[日本語](README.ja.md)

An advanced timer-ownership example for library authors: acquire a timer, set its base and attach an update ISR.

## Hardware and connections

Connect a UART monitor; no GPIO wiring is needed.

## Steps and expected results

Logs show `TIM lease: active` or `lost` and update counts. Failed acquisition or setup does not report active.

## API and processing flow

Acquire with `ch32TimerTryAcquire()`, then call `ch32TimerApplyBase()`, `ch32TimerAttachUpdateInterrupt()` and `ch32TimerStart()`. Check lease validity too.

## Adapting the example

The ISR only counts. `onQuiesce()` runs synchronously and must not re-enter timer management. Prefer [Timing](../Timing/README.md) for ordinary periodic work; see the [timer design contract (Japanese)](../../../../docs/timer-design.ja.md) for low-level use.

## Related documentation

- [Sketch](TimerResource.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
