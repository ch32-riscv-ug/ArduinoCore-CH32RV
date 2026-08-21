/* Fade - ramp an LED up and down with analogWrite().
 *
 * Wiring: an LED and a resistor on a PWM-capable pad, or the on-board LED if
 * it happens to be on one. analogWrite() falls back to a plain HIGH/LOW on a
 * pad with no timer channel, so this still runs everywhere - it just stops
 * fading.
 *
 * The PWM frequency is 1 kHz and the range is 0-255, as on AVR.
 */
static const uint8_t LED = LED_BUILTIN;

void setup()
{
    pinMode(LED, OUTPUT);
}

void loop()
{
    for (int level = 0; level <= 255; level += 5) {
        analogWrite(LED, level);
        delay(20);
    }
    for (int level = 255; level >= 0; level -= 5) {
        analogWrite(LED, level);
        delay(20);
    }
}
