# RandomNumbers

[日本語](README.ja.md)

Compare reproducible pseudorandom sequences with a changing seed.

## Hardware and connections

Connect a UART monitor. The example also uses a floating analog input to vary the seed.

## Steps and expected results

Reset and compare the repeated `seed 1:` sequence. `d6:` outputs values from 1 to 6.

## API and processing flow

`randomSeed(seed)` selects initial generator state. `random(min, max)` excludes max, so a six-sided die uses `random(1, 7)`.

## Adapting the example

ADC noise mixed with time does not guarantee unpredictable randomness. Do not use it for cryptographic keys or authentication values. Fixed seeds are useful for repeatable tests.

## Related documentation

- [Sketch](RandomNumbers.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
