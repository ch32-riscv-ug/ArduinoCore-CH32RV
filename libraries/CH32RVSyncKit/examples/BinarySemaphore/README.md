# BinarySemaphore

日本語: [README.ja.md](README.ja.md)

"It happened" once: the ISR gives, `loop()` takes. A second `give()` before the take is refused.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. A press shows `PRESSED` within a second.
2. Press twice within a second: `MISSED n` (loop() was too slow).

## Settings and limits

Use `Notify`'s counter when the count matters.

## API usage and adaptation

`give()` stores one pending signal and returns `false` if already pending. `tryTake()` removes it and returns `true`, or immediately returns `false` if absent. Choose NotifyCounter when the number of events matters.

For an exact read/reset of the auxiliary `missed` counter, copy and clear it together inside a short `CriticalSection`, then print outside. `volatile` alone does not prevent an interrupt between reading and clearing.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[BinarySemaphore.ino](BinarySemaphore.ino)
