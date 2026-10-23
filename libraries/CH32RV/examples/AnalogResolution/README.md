# AnalogResolution

[日本語](README.ja.md)

Change the numeric scales used by ADC and PWM APIs.

## Hardware and connections

Connect a potentiometer to `A1` and a resistor-limited LED or measuring instrument to `PWM_PIN`. Adapt pins to the board.

## Steps and expected results

UART shows readings requested at 8 and 12 bits. PWM alternates between the configured maximum value and zero.

## API and processing flow

`analogReadResolution(bits)` and `analogWriteResolution(bits)` select numeric ranges. The two readings are separate conversions; a changing input need not yield an exact scale ratio.

## Adapting the example

Increasing the numeric width does not improve physical ADC precision. PWM frequency is a separate setting. Consider effects on other code when changing resolution during execution.

## Related documentation

- [Sketch](AnalogResolution.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
