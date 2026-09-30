/* AnalogRead - print what the ADC sees on one pin.
 *
 * Wiring: a potentiometer between 3V3 and GND with its wiper on the pin, or
 * nothing at all - an unconnected input floats and the reading wanders, which
 * is itself a useful thing to see once.
 *
 * A1 is whatever the variant maps ADC channel 1 to; the sketch prints the pad
 * name so there is no guessing.
 */
/* A1, not A0: ADC channel 0 has no pad on CH32M007, so A0 is the one
 * analog alias that is not defined on every series. A1..A6 are. */
static const uint8_t SENSOR = A1;

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    Serial.print("reading A1, which is pad number ");
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
