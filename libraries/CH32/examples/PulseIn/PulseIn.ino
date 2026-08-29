/* PulseIn - measure how long a pin stays high.
 *
 * Wiring: anything that produces a pulse. An HC-SR04 ultrasonic module is the
 * classic: trigger it for 10 us and its ECHO pin goes high for as long as the
 * sound takes to come back.
 *
 * **HC-SR04 runs at 5 V** and its ECHO pin will drive 5 V into the CH32. Use a
 * divider, or a module that says it is 3.3 V safe.
 *
 * With nothing connected, pulseIn() returns 0 after the timeout - which is
 * what this sketch shows when it is left unwired.
 */
/* Change these two to your wiring. They are just pads that exist on the
 * series this example is built for. */
static const uint8_t TRIGGER = PC0;
static const uint8_t ECHO = PC1;

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    pinMode(TRIGGER, OUTPUT);
    pinMode(ECHO, INPUT);
    digitalWrite(TRIGGER, LOW);
}

void loop()
{
    /* 10 us is what the HC-SR04 asks for. delayMicroseconds() is a busy loop,
     * so it is accurate at this scale in a way delay() is not. */
    digitalWrite(TRIGGER, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIGGER, LOW);

    const unsigned long us = pulseIn(ECHO, HIGH, 30000ul);
    if (us == 0) {
        Serial.println("no pulse within 30 ms");
    } else {
        Serial.print(us);
        Serial.print(" us  ~= ");
        /* Sound travels about 343 m/s, and the pulse covers the distance
         * twice. 58 us per centimetre is the usual shorthand. */
        Serial.print(us / 58.0, 1);
        Serial.println(" cm");
    }
    delay(500);
}
