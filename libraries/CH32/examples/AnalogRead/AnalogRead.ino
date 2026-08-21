/* AnalogRead - print what the ADC sees on one pin.
 *
 * Wiring: a potentiometer between 3V3 and GND with its wiper on the pin, or
 * nothing at all - an unconnected input floats and the reading wanders, which
 * is itself a useful thing to see once.
 *
 * A0 is whatever the variant maps ADC channel 0 to; the sketch prints the pad
 * name so there is no guessing.
 */
static const uint8_t SENSOR = A0;

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    Serial.print("reading A0, which is pad number ");
    Serial.println(SENSOR);
}

void loop()
{
    const int raw = analogRead(SENSOR);
    /* The reference is the 3.3 V supply, and the ADC is 10 or 12 bits
     * depending on the part - analogRead() always returns the full range it
     * has, so scale by the maximum it can produce. */
    Serial.print(raw);
    Serial.print("  ");
    Serial.print((raw * 3.3) / 4095.0, 3);
    Serial.println(" V (assuming 12-bit)");
    delay(500);
}
