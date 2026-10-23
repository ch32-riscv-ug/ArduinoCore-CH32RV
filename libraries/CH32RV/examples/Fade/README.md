# Fade

[日本語](README.ja.md)

Vary PWM duty to fade an LED up and down.

## Hardware and connections

Connect an LED with a current-limiting resistor to the PWM-capable pin named `LED`.

## Steps and expected results

The LED brightens and dims repeatedly. If it only switches between two states, check PWM routing and timer conflicts.

## API and processing flow

`analogWrite(pin, value)` sets duty relative to the configured resolution. This sketch uses 0–255; it is not a DAC voltage output.

## Adapting the example

Check PWM routing when changing pins. For interaction with tone or Servo, see [timer sharing (Japanese)](../../../../docs/timer-design.ja.md).

## Related documentation

- [Sketch](Fade.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
