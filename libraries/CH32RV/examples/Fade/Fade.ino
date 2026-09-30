/* Fade - ramp an LED up and down with analogWrite().
 *
 * Wiring: an LED and a resistor on a PWM-capable pad, or the on-board LED if
 * it happens to be on one. analogWrite() falls back to a plain HIGH/LOW on a
 * pad with no timer channel, so this still runs everywhere - it just stops
 * fading.
 *
 * The PWM frequency is 1 kHz and the range is 0-255, as on AVR.
 */
/* Change this to the pad your LED is on. PA1 is a PWM pad on every series
 * this example is built for; it is the sketch's choice, not the board's. */
static const uint8_t LED = PA1;

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
