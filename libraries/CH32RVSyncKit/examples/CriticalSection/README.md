# CriticalSection

日本語: [README.ja.md](README.ja.md)

Reads several variables an ISR writes as one consistent snapshot. `CriticalSection` masks this core's interrupts for its scope only.

## Hardware and connections

A push button between `BUTTON_PIN` (default `PA1`) and GND, and a 115200-baud 8N1 UART monitor on RX/TX.

## Steps and expected results

1. `PRESSES count=... last_ms=... sum_us_low=...` every second.
2. Each press raises `count` and moves `last_ms` to its time.

## Settings and limits

Every interrupt - USB, USB PD, the UARTs - waits while it is held, so copy inside and print outside. It nests and restores the previous state. CH32V003 has no atomic instructions, so this is how a multi-word value is shared with an ISR. Inside an ISR this core's interrupts are already off; the ISR needs no guard.

## API usage and adaptation

`CH32RVSyncKit::CriticalSection cs;` restores the previous interrupt state when its scope ends. Put only the required copies inside `{ ... }`, then calculate and print from local copies afterward.

Sharing state between USB notification callbacks and `loop()` differs from ISR sharing. When a callback runs only inside `deliverEvents()`, sharing a variable with the surrounding loop alone does not require interrupt masking.

## Related documentation

- [Shared setup](../README.md)
- [CH32RVSyncKit API](../../README.md)

[CriticalSection.ino](CriticalSection.ino)
