# LogicBurst

[日本語](README.ja.md)

Capture a GPIO port once and inspect a PWM period in samples.

## Hardware and connections

Provide the target board and UART monitor. The sample generates PWM on PE3 and reads the same port input register, so no external source is needed.

## Steps and expected results

Each capture prints estimated rate, rising-edge count and samples per period. Compare with the expected PWM-to-sampling-rate ratio.

## API and processing flow

`logic.begin(port, rate)` selects the port. `captureBurst(buffer, samples)` captures once and returns the observed rate, or zero on failure. Bit n of `buf[i]` corresponds to pin n.

## Adapting the example

Burst timing follows DMA rather than a periodic timer and varies with bus contention. Consider the effect of disabling interrupts during capture. Check pin supply domains and voltage ratings before connecting external signals.

## Related documentation

- [Sketch](LogicBurst.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
