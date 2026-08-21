/* PinCapabilities - what this particular chip actually has.
 *
 * Wiring: none.
 *
 * CH32 pin numbers are not 0..N. A pin is (port << 5) | bit, so PA0 is 0, PB0
 * is 32 and PC13 is 77 - and most numbers in between belong to no pad at all.
 * The variant knows which are real, and the sketch asks it rather than
 * guessing.
 *
 * This is the example to run first on a board you do not know.
 */
static void describe(uint8_t pin, const char *name)
{
    Serial.print(name);
    Serial.print(" = ");
    Serial.print(pin);
    if (!digitalPinIsValid(pin)) {
        Serial.println("   (no such pad on this series)");
        return;
    }
    Serial.print(digitalPinIsCommon(pin) ? "   on every part"
                                         : "   only on some packages");
#ifdef NUM_ANALOG_INPUTS
    if (digitalPinHasADC(pin)) {
        Serial.print(", ADC channel ");
        Serial.print(digitalPinToAnalogChannel(pin));
    }
#endif
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }

    Serial.println();
    Serial.println("--- pads this sketch was built for ---");
    describe(LED_BUILTIN, "LED_BUILTIN");
    describe(A0, "A0");
#ifdef PIN_WIRE_SDA
    describe(PIN_WIRE_SCL, "SCL");
    describe(PIN_WIRE_SDA, "SDA");
#endif
#ifdef PIN_SPI_SCK
    describe(PIN_SPI_SCK, "SCK");
    describe(PIN_SPI_MISO, "MISO");
    describe(PIN_SPI_MOSI, "MOSI");
#endif

    Serial.println();
    Serial.print("ports on this series: ");
    Serial.println(CH32_PORT_COUNT);
    Serial.print("analog inputs: ");
#ifdef NUM_ANALOG_INPUTS
    Serial.println(NUM_ANALOG_INPUTS);
#else
    Serial.println(0);
#endif

    /* Every pad the variant says exists, as a map. */
    Serial.println();
    Serial.println("--- every valid pad ---");
    for (uint8_t port = 0; port < CH32_PORT_COUNT; port++) {
        Serial.print('P');
        Serial.print((char)('A' + port));
        Serial.print(": ");
        for (uint8_t bit = 0; bit < 24; bit++) {
            const uint8_t pin = (uint8_t)((port << 5) | bit);
            if (digitalPinIsValid(pin)) {
                Serial.print(bit);
                Serial.print(' ');
            }
        }
        Serial.println();
    }
}

void loop()
{
}
