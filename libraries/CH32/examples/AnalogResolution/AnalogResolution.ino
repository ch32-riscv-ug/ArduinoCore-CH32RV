/* AnalogResolution - change the numbers analogRead() and analogWrite() speak.
 *
 * Wiring: none required; a potentiometer on A0 makes the readings move.
 *
 * The hardware does not change - the ADC is 10 bits on CH32V003 and 12 on the
 * rest, and PWM is always 8 bits of duty at 1 kHz. What changes is the scale
 * the API uses, which is there so code written for another board keeps its
 * numbers.
 */
void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    /* The default read resolution is whatever the part gives. Asking for 8
     * bits scales the answer down; asking for 16 scales it up - it does not
     * invent precision the ADC does not have. */
    analogReadResolution(8);
    const int eight = analogRead(A0);
    analogReadResolution(12);
    const int twelve = analogRead(A0);

    Serial.print("A0 at 8 bits: ");
    Serial.print(eight);
    Serial.print("   at 12 bits: ");
    Serial.println(twelve);

    /* Write resolution works the same way: with 10 bits, 1023 is full duty. */
    analogWriteResolution(10);
    analogWrite(LED_BUILTIN, 1023);
    delay(300);
    analogWrite(LED_BUILTIN, 0);
    analogWriteResolution(8);       /* back to the Arduino default */
    delay(700);
}
