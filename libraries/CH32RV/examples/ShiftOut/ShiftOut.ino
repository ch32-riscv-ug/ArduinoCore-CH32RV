// en: Drive a 74HC595 shift register and move one active output across its bits.
// ja: 74HC595へ値を送り、有効な出力を順番に移す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

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
    // en: Clock out the most significant bit first; the receiving chip shifts in one bit per clock.
    // ja: 上位bitから順に出す。相手のICはclockごとに1 bitずつ取り込む。
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
