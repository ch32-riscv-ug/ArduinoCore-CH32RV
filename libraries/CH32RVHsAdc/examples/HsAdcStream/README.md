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

### Host receiver and stream format

Use a USB path with enough bandwidth for the selected sample rate. The sample uses default identity `1209:0001`; select its class `0xff` interface and discover bulk IN from the descriptors rather than hard-coding endpoint addresses. Queue several asynchronous libusb reads so gaps between synchronous calls do not repeatedly idle the endpoint.

The stream contains raw 16-bit little-endian sample values, with no application message header. The example's ADC values use the range 0–1023. Required payload bandwidth is `sampleRate() × sizeof(uint16_t)` bytes/s. The sent/lost counters count buffers, not samples, and do not identify the positions of missing samples in the raw stream. These counters are cumulative.

## Related documentation

- [Sketch](HsAdcStream.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
