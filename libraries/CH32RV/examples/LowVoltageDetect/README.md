# LowVoltageDetect

[日本語](README.ja.md)

The programmable voltage detector (PVD): a callback when the supply falls below a threshold, on the PVD's EXTI line. Only a UART connection is needed; a variable supply shows a real fall. On a chip without a PVD interrupt (H41x, M030, V004 / V007 / M007) the sketch says so.

## Operation

1. Open the terminal and reset the board. The sketch prints the PVD's levels, the threshold it chose for 3000 mV and the supply (`getVddMillivolts()`).
2. Lower the supply below the threshold: `Low voltage #1 (still low)` appears. Where the supply is already below it (on a series whose levels all lie under 3000 mV the highest is taken), it appears at once.
3. Send `t`: the PVD's line is raised from software (`triggerExtiLine()`), and the callback runs as on a real fall.

## API and adaptation notes

`CH32RV.setLowVoltageDetect(mv, callback)` picks the lowest threshold at or above `mv` (the highest when `mv` is above them all) and returns it in mV. The callback runs in the PVD's interrupt each time VDD falls below it, and once at once when VDD already is. It is a warning, not a reset: unlike ESP32's brownout detector the chip keeps running, and the sketch may save its state. `isLowVoltage()` reads the PVD's output now; near the threshold it may fall several times. The levels are the datasheet's typical values, a few per series (`lowVoltageLevel(i)`).

The same callback path serves the other EXTI lines that are not pins: `CH32RV.attachExtiLine(CH32RV_EXTI_USBFS_WAKEUP, callback)` and the like, with `enableExtiLineWakeup()` to let the line end a sleep. The names exist where the series has the line (`#ifdef CH32RV_EXTI_PVD`).

## Before running

Select your board and exact Part Number. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND, at compatible I/O voltages. Keep the supply within the chip's operating range while lowering it.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
