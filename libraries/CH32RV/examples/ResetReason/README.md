# ResetReason

[日本語](README.ja.md)

Compare a normal reset with a software reset requested over UART. No extra component is needed.

## Operation and API

Open the terminal, reset the board, and read `Reset reason`. Send lowercase `r`: the sketch finishes the outgoing UART message with `Serial.flush()`, calls `CH32RV.restart()`, then starts again. The next startup reports the software-reset reason.

`restart()` resets the chip and never returns. Code after it cannot perform cleanup. `resetReasonName()` gives a readable name; use `resetReason()` and the `CH32RV_RESET_*` values for program decisions. Reading the reason repeatedly in one boot gives the same latched result.

The sample restarts only on command. If you adapt it for fault recovery, avoid restarting unconditionally in `setup()`. A reset cannot fix incorrect wiring or an external device holding a bus low. If the first report is missing, open the terminal before resetting; changing USB-UART control lines may itself reset some boards.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
