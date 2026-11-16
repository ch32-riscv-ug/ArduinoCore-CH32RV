# RtcAlarm

[日本語](README.ja.md)

The RTC as a calendar, its alarm, sleeping until the alarm, and a backup register that counts boots. Only a UART connection is needed. On a chip without an RTC (V003, V00x, M030, X033/X035) the sketch says so and only prints the time since power-up.

## Operation

1. Open the terminal and reset the board. The sketch prints the reset reason, the wakeup cause and the boot number from backup register 0 (it goes up by one at every reset).
2. After a power cut the RTC starts at 1970: the sketch sets 2026-01-01 then. It prints the date, and every 10 seconds `Alarm at` with the date.
3. Send `s`: the chip sleeps until the next alarm (`enableAlarmWakeup()`, `lightSleep()`) and prints the result and the wakeup cause (3, `CH32RV_WAKEUP_ALARM`).

## API and adaptation notes

The calendar is POSIX time, as on arduino-esp32: `time()`, `localtime_r()`, `strftime()`, `mktime()` and `settimeofday()` (SNTP's `configTime()` sets it too). `CH32RV.setAlarm(when, callback)` takes `time()`'s seconds or a `struct tm` in local time. The callback runs once, in an interrupt, within a second after the time (the RTC counts whole seconds), so it only sets a flag here; set the next alarm from `loop()`. There is one alarm: a new `setAlarm()` replaces it.

The RTC runs from the internal LSI oscillator, which the core measures at the first start: expect seconds per day of drift. The backup registers are 16-bit words (`backupRegisterCount()` of them); on H41x and X305/X315 they are RAM that only a reset keeps. `deepSleep()` with `enableAlarmWakeup()` sleeps in standby where there is one and starts again from `setup()`; the callback does not run then, and `wakeupCause()` says the alarm woke it.

## Before running

Select your board and exact Part Number. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND, at compatible I/O voltages. A debugger attached through SWD may lose the chip while it sleeps.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
