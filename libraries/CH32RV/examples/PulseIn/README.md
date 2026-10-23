# PulseIn

[日本語](README.ja.md)

Generate a trigger and measure the duration of a High input pulse.

## Hardware and connections

Connect `TRIGGER`, `ECHO` and common GND to a responding device. Level-shift inputs exceeding GPIO ratings. Connect a UART monitor.

## Steps and expected results

A pulse produces a duration in µs; otherwise a timeout message appears. The distance conversion assumes an ultrasonic sensor.

## API and processing flow

`delayMicroseconds()` takes µs; `pulseIn(pin, HIGH, timeout)` also returns µs. Zero means no valid pulse width was obtained.

## Adapting the example

`pulseIn()` waits for a result or timeout. Keep the timeout short with USB and other concurrent work, or record edge times in interrupts instead. Distance estimates depend on conditions such as temperature and target.

### Using an HC-SR04

The trigger sequence and distance conversion assume an HC-SR04-style ultrasonic sensor. A 5 V ECHO must not be connected to an input that cannot tolerate it: use a divider or suitable level conversion. The conversion assumes an approximate sound speed; the measured pulse covers the round trip.

## Related documentation

- [Sketch](PulseIn.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
