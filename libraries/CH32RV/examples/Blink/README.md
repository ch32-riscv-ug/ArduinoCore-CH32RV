# Blink

[日本語](README.ja.md)

Toggle a GPIO to confirm sketch execution with an LED.

## Hardware and connections

Use the board LED, or connect an LED in series with a current-limiting resistor between the GPIO and GND. Specify the actual pin as `LED_BUILTIN`.

## Steps and expected results

After upload, the LED alternates at a regular interval. Boards with active-low LEDs reverse the visible on/off order.

## API and processing flow

`pinMode(..., OUTPUT)` selects output mode, and `digitalWrite()` sets the logic level. `delay()` takes milliseconds.

## Adapting the example

Generic boards do not define a PCB LED position. If `LED_BUILTIN` is missing, define the actual LED pin. Move to time comparisons in [Timing](../Timing/README.md) when handling multiple inputs.

### Defining the LED for a Generic board

For example, if the schematic places the LED on PC13, compile from the repository root with the LED definition:

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32V203 \
  --build-property build.extra_flags=-DLED_BUILTIN=PC13 \
  libraries/CH32RV/examples/Blink
```

Replace both the board selection and PC13 with your actual target. An equivalent sketch-level definition must precede the `#ifndef LED_BUILTIN` check.

## Related documentation

- [Sketch](Blink.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
