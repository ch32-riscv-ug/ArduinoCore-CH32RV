# Servo

Up to twelve RC servos on any pins. One timer drives them in turn: it raises a
servo's pin, interrupts when that servo's pulse is over, lowers it and moves on,
then idles out the rest of the 20 ms frame. That is how the AVR library works,
and it is why the pins are not restricted to a timer's compare outputs.

```cpp
#include <Servo.h>

Servo servo;

void setup() {
  servo.attach(PA1);
  servo.write(90);            // degrees
}

void loop() {
  servo.writeMicroseconds(1500);   // or microseconds, if you prefer
}
```

## Things worth knowing

- **Power the servo separately.** A servo's stall current will brown out the
  board's regulator, and the symptom is a board that resets mid-sweep. Share
  the ground, not the 3V3 rail.
- **Which timer, and what it costs.** The variant picks it and names it as
  `CH32_SERVO_TIMER`. It is never the timer `tone()` uses, so a buzzer and a
  servo can run at once. On the smaller parts there is no timer left over, and
  then it shares one with `analogWrite()` - the variant header lists the pads
  that stop fading while a servo is attached.
- **`attach()` can fail.** It returns `INVALID_SERVO` when the pin is not a
  pin, when all twelve slots are taken, or when the series has no timer to
  spare. A sketch that ignores the return value will silently do nothing.
- **`write()` below 544 is an angle, at or above it a pulse width.** That
  ambiguity is the AVR library's; it is kept so sketches behave the same.
- The default range is 544-2400 us for 0-180 degrees. `attach(pin, min, max)`
  changes it per servo - most servos want less than the full range.
- `detach()` frees the slot and leaves the pin low; the timer stops when the
  last servo detaches.

## Examples

- **Sweep** - end to end and back, the classic.
