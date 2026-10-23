# PinInterrupt

[日本語](README.ja.md)

Count GPIO edges in an interrupt and defer printing to `loop()`.

## Hardware and connections

Connect a button between `BUTTON` and GND. `INPUT_PULLUP` makes released High and pressed Low. Connect a UART monitor.

## Steps and expected results

Button activity increases `edges:`. Contact chatter can make the interrupt count differ from the number of presses.

## API and processing flow

`attachInterrupt(digitalPinToInterrupt(pin), handler, FALLING)` registers falling edges. The ISR only increments a `volatile` counter; `loop()` copies it once before displaying.

## Adapting the example

Do not print or wait in the ISR. `volatile` does not make multiple operations atomic. Use [CH32RVSyncKit](../../../CH32RVSyncKit/README.md) for larger state transfers. EXTI lines with the same bit number can conflict.

## Related documentation

- [Sketch](PinInterrupt.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
