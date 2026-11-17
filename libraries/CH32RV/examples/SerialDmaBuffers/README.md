# SerialDmaBuffers

[日本語](README.ja.md)

Receive a 1 Mbaud stream while `loop()` spends 4 ms at a time elsewhere. `setRxBufferSize()` and `setTxBufferSize()`
(ESP32's names) give `Serial` larger buffers, and DMA moves their bytes instead of one interrupt per byte.

## Hardware and connections

A USB-UART adapter that runs at 1 Mbaud (CP2102N, FT232R, CH343 and the like) on `Serial`: adapter RX to board TX,
TX to RX, common GND.

## Steps and expected results

Open the adapter at 1000000 8N1 and send a file (a few hundred kilobytes). Every second the board prints the bytes
received so far and their sum; the count ends at the file's size and no `receive error mask` line appears. Change
`setRxBufferSize(512)` to `setRxBufferSize(32)`: the 4 ms away now overruns the buffer, and `receive error mask 0x4`
(`UART_BUFFER_FULL_ERROR`) shows bytes lost.

## API and processing flow

`setRxBufferSize()` / `setTxBufferSize()` come before `begin()` (as on an ESP32; they return 0 on an open port). A
size up to the built-in 64 bytes uses that storage, a larger one the heap. The receive DMA runs in a circle from
`begin()` to `end()`, so bytes keep coming in even with interrupts off; `available()` and `read()` take its place when
they look. When the sketch stays away longer than the buffer holds, the newest bytes overwrite the oldest and the next
look reports `UART_BUFFER_FULL_ERROR`. On transmit, `print()` fills the buffer and the DMA sends it in runs.

## Adapting the example

Size the buffer for the longest time the sketch is away from `Serial`: baud / 10 bytes a second. CH32V003 has 2 KB of
RAM in all. Each USART has fixed DMA channels (CH32H41x: chosen by the core); when CH32RVAdcStream or
CH32RVLogicCapture holds the same channel, that direction falls back to the built-in buffer and interrupts, as it does
with RTS flow control.

## Related documentation

- [Sketch](SerialDmaBuffers.ino)
- [Basic example setup and index](../README.md)
