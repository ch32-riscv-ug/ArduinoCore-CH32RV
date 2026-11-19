# RandomNumbers

[日本語](README.ja.md)

Compare reproducible pseudorandom sequences with a changing seed.

## Hardware and connections

Connect a UART monitor.

## Steps and expected results

Reset and compare: the `seed 1:` sequence repeats, the one seeded from `ch32rv_random()` and the random bytes change from run to run. `d6:` outputs values from 1 to 6.

## API and processing flow

`randomSeed(seed)` selects initial generator state; without it `random()` gives the same sequence every run, as on other Arduino boards. `random(min, max)` excludes max, so a six-sided die uses `random(1, 7)`. `ch32rv_random()` and `ch32rv_fill_random(buf, len)` are arduino-esp32's `esp_random()` and `esp_fill_random()`: words from the hardware random number generator on V30x, V4x7 and H41x (`CH32RV_RNG_BASE` is defined there).

## Adapting the example

Where there is no hardware generator, `ch32rv_random()` mixes the chip's unique ID with the time of the call: it differs from chip to chip and run to run, but it is predictable and must not be used for cryptographic keys or authentication values. `random()` itself is never cryptographic. Fixed seeds are useful for repeatable tests.

## Related documentation

- [Sketch](RandomNumbers.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
- [Sketch composition guide (Japanese)](../../../../docs/sketch-guide.ja.md)
