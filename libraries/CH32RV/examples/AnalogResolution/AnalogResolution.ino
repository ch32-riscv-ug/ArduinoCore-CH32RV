/* AnalogResolution - change the numbers analogRead() and analogWrite() speak.
 *
 * Wiring: none required; a potentiometer on A1 makes the readings move.
 *
 * The hardware does not change - the ADC is 10 bits on CH32V003 and 12 on the
 * rest, and PWM is always 8 bits of duty at 1 kHz. What changes is the scale
 * the API uses, which is there so code written for another board keeps its
 * numbers.
 */
/* Change this to a pad you can watch. PA1 is a PWM pad on every series this
 * example is built for; it is the sketch's choice, not the board's. */
static const uint8_t PWM_PIN = PA1;

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    pinMode(PWM_PIN, OUTPUT);
}

void loop()
{
    /* The default read resolution is whatever the part gives. Asking for 8
     * bits scales the answer down; asking for 16 scales it up - it does not
     * invent precision the ADC does not have. */
    analogReadResolution(8);
    const int eight = analogRead(A1);
    analogReadResolution(12);
    const int twelve = analogRead(A1);

    Serial.print("A1 at 8 bits: ");
    Serial.print(eight);
    Serial.print("   at 12 bits: ");
    Serial.println(twelve);

    /* Write resolution works the same way: with 10 bits, 1023 is full duty. */
    analogWriteResolution(10);
    analogWrite(PWM_PIN, 1023);
    delay(300);
    analogWrite(PWM_PIN, 0);
    analogWriteResolution(8);       /* back to the Arduino default */
    delay(700);
}
