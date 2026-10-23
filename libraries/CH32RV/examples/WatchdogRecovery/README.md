# WatchdogRecovery

[日本語](README.ja.md)

Observe the independent watchdog resetting the chip after the application stops feeding it. Only the UART connection is needed.

## Operation

1. Reset and read the reason. The watchdog is not started automatically by this sketch.
2. Send `e`. If `wdtEnable(2000)` succeeds, the loop feeds the watchdog with `wdtFeed()` and the board keeps running.
3. Send `x`. Feeding stops while `loop()` continues. After the watchdog expires, startup runs again and reports the watchdog reset.
4. The restarted sketch waits for `e` again, allowing inspection without a reset loop. If the API reports unavailable, this demonstration cannot run on that target.

## API and adaptation

The timeout argument is milliseconds, but the actual interval depends on the watchdog clock and hardware range. Do not use the reset delay as a precise timer. Once started, this watchdog has no disable API. Sending `e` again does not stop it, and after `x` the example intentionally offers no resume command.

Feed only after the application has made the progress you want to supervise. Feeding from an independent periodic interrupt can hide a stuck foreground task. Keep normal work and UART output short enough to allow feeding; adding a long blocking operation changes what this example demonstrates. Resetting does not preserve ordinary RAM variables, so an operation requiring recovery must store or reconstruct its own state.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
