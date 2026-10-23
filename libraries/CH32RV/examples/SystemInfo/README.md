# SystemInfo

[日本語](README.ja.md)

Print the build's target series, CPU frequency, reset reason, sketch size and heap information. This example needs only the UART connection.

## Operation and API

Reset the board with the terminal open to see the report. Send `i` to read the free heap again. The sketch does not allocate memory to demonstrate exhaustion.

- `getChipModel()` identifies the series the binary was built for; it is not a board or package detector.
- `getSketchSize()` and `getFreeSketchSpace()` describe the linked program and its selected Flash region. Free space is not a persistent-storage allocation.
- `getHeapSize()` and `getFreeHeap()` report bytes. Fragmentation can prevent a single allocation as large as the free total.
- When `CH32RV_ESIG_FLACAP_ADDR` is defined, the sketch also calls `getFlashChipSize()` and `getUniqueId()`. Otherwise it reports that the signature is unavailable. The UID is displayed as high/low hexadecimal halves without zero padding; it is neither a MAC address nor a secret.

If the signature's Flash size differs from the linked capacity, check the selected Part Number. Runtime values belong to this build and chip; do not treat the sample output as a specification for another board.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
