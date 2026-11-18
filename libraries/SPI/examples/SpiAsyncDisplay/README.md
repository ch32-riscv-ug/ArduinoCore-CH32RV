# SpiAsyncDisplay

[日本語](README.ja.md)

Keeps drawing bands on an ST7789 display (240x240, 16-bit colour). It holds two buffers of 8 lines each and draws
the next 8 lines into one while the other goes out by `SPI.transferAsync()` (DMA). The time of a frame goes to
`Serial`.

## Wiring and operation

Wire the display's SCK and MOSI to SPI1's default pins, CS to `PA4`, DC to `PA3` and RST to `PA2`, with power and a
common GND (MISO is not used). The constants at the top of the sketch change the pins. Check the display's voltage
(mostly 3.3 V) and how its backlight is wired in that board's documents.

## API and changing it

- `transferAsync()` returns as soon as the transfer starts. Until `finishedAsync()` is `true`, leave that buffer
  alone and keep other transfers off the bus. The example waits for the previous transfer before sending a buffer
  it has finished drawing.
- When `transferAsync()` is `false` (another user holds that SPI's DMA channel, or the SPI is software), the same
  buffer goes out there and then by `writeBytes()`. The picture is the same; drawing and sending just do not overlap.
- The pixels sit in the buffer in the display's byte order (high byte first).
- 24 MHz asked may come out slower through the SPI's divider ("Things worth knowing" in [SPI](../../README.md)).
- In an LVGL flush, call `transferAsync()` there and wait with `finishedAsync()` in
  `lv_display_set_flush_wait_cb()`: LVGL then draws the next part while this one is sent.

## Before running it

Pick the board and the exact Part Number and check the sketch's pins against the schematic. The log is the hardware
`Serial` at 115200 baud, 8N1. The generated `sketch.yaml` has the build profiles; a successful build is not a check
of the wiring or of the hardware. See also the [build rules](../../../../docs/examples-build-rules.ja.md) and the
[upload routes](../../../../docs/upload-routes.ja.md).
