# WindowWatchdog

[日本語](README.ja.md)

The window watchdog resets the chip when it is fed too late - and also when it is fed too early. This sketch starts it on command, feeds it inside its window, and lets you break either rule. Only a UART connection is needed.

## Operation

1. Read the reset reason and the timeout the sketch chose: the shorter of 40 ms and `CH32RV.wwdgMaxTimeoutUs()`, which depends on the bus clock (about 43 ms at 48 MHz). The window opens halfway.
2. Send `e`. `wwdgEnable(timeout, window, earlyWarning)` starts it; `loop()` feeds it three quarters of the way to the timeout, inside the window, and the board keeps running.
3. Send `x` to stop feeding: the chip resets after the timeout. The next start reports `window_watchdog` and that the early-warning callback ran once before the reset.
4. Or send `f` to feed at one eighth of the timeout, before the window opens: the chip resets at that feed, and the early-warning callback did not run (0).

If the sketch says `Window watchdog refused`, the timeout was outside what this clock allows; the experiment cannot run.

## API and adaptation notes

The window watchdog counts the APB clock (PCLK1 / 4096 / 1-8) and allows only 63 counts, so its timeouts are milliseconds, not seconds; use `wdtEnable()` (the independent watchdog) for long ones. Both edges are exact to one count. Once started it cannot be stopped, and neither can its early warning. The callback runs in an interrupt one count before the reset: feed there to keep running, or record why, as this sketch does, in RAM the startup does not clear (`.noinit`) with a check word. Ordinary variables are reset.

Feeding from a fixed timer interrupt defeats the purpose: feed where the work you want to watch has made progress. The UART output and any long wait between feeds must stay well inside the window - this sketch prints nothing while the watchdog runs. `resetReason()` tells this reset (`CH32RVResetReason::WindowWatchdog`) from the independent watchdog's (`CH32RVResetReason::Watchdog`).

## Before running

Select your board and exact Part Number. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND, at compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
