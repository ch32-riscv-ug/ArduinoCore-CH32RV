// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static void describe(uint8_t pin, const char *name)
{
    Serial.print(name);
    Serial.print(" = ");
    Serial.print(pin);
    // en: Ask the variant about a pad rather than treating numbers as consecutive pins.
    // ja: 端子番号を連番と仮定せず、variantにpadの有無を問い合わせる。
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
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }

    Serial.println();
    Serial.println("--- pads this sketch was built for ---");
#ifdef LED_BUILTIN

    describe(LED_BUILTIN, "LED_BUILTIN");
#endif
#ifdef A0

    describe(A0, "A0");
#endif
    describe(A1, "A1");
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
    Serial.println(CH32RV_PORT_COUNT);
    Serial.print("analog inputs: ");
#ifdef NUM_ANALOG_INPUTS
    Serial.println(NUM_ANALOG_INPUTS);
#else
    Serial.println(0);
#endif

    Serial.println();
    Serial.println("--- every valid pad ---");
    // en: This reports build-time pin data, not electrically detected connections.
    // ja: 表示するのはビルド時の端子情報であり、電気的な接続検出ではない。
    for (uint8_t port = 0; port < CH32RV_PORT_COUNT; port++) {
        Serial.print('P');
        Serial.print((char)('A' + port));
        Serial.print(": ");
        for (uint8_t bit = 0; bit < 24; bit++) {
            const uint8_t pin = (uint8_t)CH32RV_PIN(port, bit);
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
