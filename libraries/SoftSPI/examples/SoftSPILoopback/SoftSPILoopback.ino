// en: Check software SPI loopback under each clock mode.
// ja: ソフトウェアSPIの各clock modeでループバックを確認する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SoftSPI.h>

static const uint8_t SCK_PIN = PA1;
static const uint8_t MOSI_PIN = PA2;
#if defined(PA4)
static const uint8_t MISO_PIN = PA4;
#else
static const uint8_t MISO_PIN = PC4;
#endif

// en: Constructor order is clock, input, output, matching SPI.begin().
// ja: constructorの順はclock、input、outputでSPI.begin()と同じ。
SoftSPI bus(SCK_PIN, MISO_PIN, MOSI_PIN);

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    bus.begin();

    Serial.println();
    Serial.print("SoftSPI on SCK=pad ");
    Serial.print(SCK_PIN);
    Serial.print(", MOSI=pad ");
    Serial.print(MOSI_PIN);
    Serial.print(", MISO=pad ");
    Serial.println(MISO_PIN);
    Serial.println("connect MOSI to MISO for the loopback");
}

void loop()
{
    static const uint8_t sent[] = {0x00, 0x55, 0xAA, 0xFF, 0x0F};

    // en: Check the same loopback data under each clock polarity/phase combination.
    // ja: clockの極性と位相の各組み合わせで、同じデータを照合する。
    for (uint8_t mode = 0; mode < 4; mode++) {
        // en: Apply this mode before shifting bits; transfer() generates the clock and samples the input.
        // ja: bitを送る前にmodeを設定する。transfer()がclockを作り、入力も読み取る。
        bus.beginTransaction(SPISettings(1000000, MSBFIRST, (SPIMode)mode));
        bool ok = true;
        for (unsigned i = 0; i < sizeof sent; i++) {
            if (bus.transfer(sent[i]) != sent[i]) {
                ok = false;
            }
        }
        bus.endTransaction();

        Serial.print("mode ");
        Serial.print(mode);
        Serial.println(ok ? ": loopback ok" : ": no loopback (is the jumper on?)");
    }
    delay(1000);
}
