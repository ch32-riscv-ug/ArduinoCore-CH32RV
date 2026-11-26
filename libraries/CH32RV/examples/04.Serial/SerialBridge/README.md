# SerialBridge

[日本語](README.ja.md)

A bridge between `Serial` and `Serial1`: bytes either one receives go out on the other. Put it between a PC and a module that talks UART (GPS, Wi-Fi, Bluetooth, another microcontroller) to drive the module from the PC.

## Wiring and operation

`Serial` (the UART or USB the PC sees) and `Serial1`'s TX / RX crossed to the module's RX / TX, GND shared, the I/O voltages matched. `Serial1`'s pads are the variant's default route; `Serial1.setPins()` changes them. What you type in the PC's terminal reaches the module and its answers come back. Both run at `BAUD` (115200).

## API and notes for changes

`available()` gives what has come, `availableForWrite()` the room in the other side's transmit buffer; `readBytes()` and `write()` move that much. Neither direction waits, so one side backing up does not stop the other. For two different rates, grow the slower side's buffer with `setTxBufferSize()` before `begin()`.

## Before running

Choose the board and the exact Part Number and check the pads against the schematic. The generated `sketch.yaml` has the build profiles; a build that passes is not a check of the wiring or of the board.
