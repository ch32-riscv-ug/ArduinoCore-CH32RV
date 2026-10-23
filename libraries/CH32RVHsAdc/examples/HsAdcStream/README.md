# HsAdcStream

[日本語](README.ja.md)

Stream continuous high-speed ADC samples directly through USB Vendor bulk IN.

## Hardware and connections

Provide target-board USB data, UART monitoring and PC0 input. Check the VIO18 requirements in [HsAdcRead](../HsAdcRead/README.md) before changing its supply.

## Steps and expected results

Read the vendor bulk IN endpoint as 16-bit little-endian samples. Observe sent, lost, overruns and host-busy counters on UART.

## API and processing flow

`startContinuous(bufA, bufB, samples)` fills two arrays alternately. Pass a completed `readBuffer()` result to `stream.writeDirect(pointer, byteLength)`. Distinguish samples from bytes.

## Adapting the example

`writeDirect()` accepts a transfer; returning does not mean USB released the buffer. Transmission must finish before ADC reuses it. When the host is slow, counters alone cannot guarantee integrity: add completion tracking, buffering and a capture-stop or discard policy.

## Related documentation

- [Sketch](HsAdcStream.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
