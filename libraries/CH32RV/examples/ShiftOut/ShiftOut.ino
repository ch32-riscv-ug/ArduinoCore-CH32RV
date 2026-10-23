// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

static const uint8_t DATA_PIN = PA1;
static const uint8_t CLOCK_PIN = PA2;
#if defined(PA4)
static const uint8_t LATCH_PIN = PA4;
#else
static const uint8_t LATCH_PIN = PC4;
#endif

void setup()
{
    pinMode(DATA_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
}

static void write595(uint8_t value)
{
    // en: Latch only after shifting the whole byte to avoid intermediate outputs.
    // ja: 途中の値を出さないよう、1 byte送信後にまとめてlatchする。
    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, value);
    digitalWrite(LATCH_PIN, HIGH);
}

void loop()
{
    for (uint8_t bit = 0; bit < 8; bit++) {
        write595((uint8_t)(1u << bit));
        delay(120);
    }
}
