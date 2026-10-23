# Sweep

[日本語](README.ja.md)

Command a servo through a range of angles and back.

## Hardware and connections

Connect its signal to `SERVO_PIN`, power it from a suitable external supply and share GND with the MCU. Do not draw motor current from the board I/O supply.

## Steps and expected results

Check the sweep and UART initialization failure message if present. Reduce the commanded range if the mechanism reaches its stops.

## API and processing flow

Check `servo.attach(pin)` against `INVALID_SERVO`. `write(angle)` commands a position; it neither detects arrival nor waits for it.

## Adapting the example

Delays allow time for movement. Check actual travel/current requirements and [timer sharing (Japanese)](../../../../docs/timer-design.ja.md) with PWM and other users.

## Related documentation

- [Sketch](Sweep.ino)
- [Library API](../../README.md)
- [Example guide](../../../../docs/examples.md)
