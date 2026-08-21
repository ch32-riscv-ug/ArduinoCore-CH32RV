/* Timing - millis(), micros() and delayMicroseconds(), and where they end.
 *
 * Wiring: none.
 *
 * Three things worth knowing, all of which this sketch shows:
 *
 * 1. millis() and micros() come from SysTick, which this core runs at 1 kHz.
 *    delay() is built on millis().
 * 2. **micros() wraps about every 71 minutes** (2^32 microseconds), and
 *    millis() about every 49 days. Subtracting two readings stays correct
 *    across the wrap; comparing them with < does not.
 * 3. delayMicroseconds() is a busy loop, not a sleep. It blocks interrupts
 *    from doing useful work but is accurate at microsecond scale.
 */
void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }

    /* How long a delayMicroseconds() actually takes, measured with micros(). */
    const unsigned long before = micros();
    delayMicroseconds(1000);
    const unsigned long after = micros();
    Serial.print("delayMicroseconds(1000) took ");
    Serial.print(after - before);        /* subtraction, so a wrap is harmless */
    Serial.println(" us");

    Serial.print("micros() wraps every ");
    Serial.print((0xFFFFFFFFul / 1000000ul) / 60ul);
    Serial.println(" minutes");
}

void loop()
{
    /* The pattern to copy: hold the last time and subtract. This keeps working
     * across the wrap, unlike `if (millis() > next)`. */
    static unsigned long last;
    const unsigned long now = millis();
    if (now - last >= 1000ul) {
        last = now;
        Serial.print("up ");
        Serial.print(now / 1000ul);
        Serial.println(" s");
    }
}
