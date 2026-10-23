# LogicStream

[日本語](README.ja.md)

Stream completed halves of a continuous GPIO capture through USB Vendor bulk IN.

## Hardware and connections

Provide target-board USB data and UART monitoring. The sample generates a PE3 test PWM. Check input voltage limits before adding external signals.

## Steps and expected results

Read bulk IN as 16-bit little-endian bitmaps. Check the period on bit 3 and the UART sent/lost counters.

## API and processing flow

`logic.startContinuous(buf, samples)` uses one array in alternating halves. `readBuffer()` returns one completed half. The direct-write length is half the sample count times `sizeof(uint16_t)`.

## Adapting the example

Avoid capture DMA overwriting memory while USB reads it. Keep data unchanged until transmission ends, before DMA returns to that half. Available processing time is half-buffer samples divided by actual sample rate. For guarantees, add completion management and stop/discard behavior on overload.

## Related documentation

- [Sketch](LogicStream.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
