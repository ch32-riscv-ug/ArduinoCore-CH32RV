# TimerAlarm

[日本語](README.ja.md)

Run a function every second on a hardware timer's alarm, with arduino-esp32's timer API (`timerBegin()`, `timerAttachInterrupt()`, `timerAlarm()`), declared by the core like the ESP32's: no include needed.

## Steps and expected results

Every second it prints `Alarm 1, count at it 0, 3 ms into the period`: the alarm resets the count to 0 (autoreload), so the count the callback reads is a few ticks past it, and `timerReadMillis()` in `loop()` shows how far the period has gone when the line is printed.

## API and adaptation

- `timerBegin(frequency)` takes a free timer counting at `frequency` (the timer clock divided by a whole number; `timerGetFrequency()` says which) and starts it. The count is 64 bits.
- `timerAlarm(timer, value, autoreload, reload)` runs the callback at `value`; with autoreload the count becomes `reload` there and the alarm repeats. `timerWrite()`, `timerRead()`, `timerStop()` / `timerStart()` and `timerEnd()` are as on the ESP32.
- The callback runs in the timer's interrupt. For several periodic functions, the [Ticker](../../../Ticker/README.md) library shares one timer between them.

## Before running

Output is hardware `Serial` at 115200 baud, 8N1. The generated `sketch.yaml` supplies build profiles. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
