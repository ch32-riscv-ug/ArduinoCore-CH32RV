// en: Check hardware SPI by looping transmitted bytes back into the receiver.
// ja: ハードウェアSPIの送信を受信へ折り返し、値を照合する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SPI.h>

static const int SCK_PIN = SCK;
static const int MISO_PIN = MISO;
static const int MOSI_PIN = MOSI;
static const int RX_PIN = RX;
static const int TX_PIN = TX;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    while (!Serial) {
    }
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);

    Serial.println();
    Serial.print("SPI");
    Serial.print(SPI.peripheral());
    Serial.print(" on SCK=pad ");
    Serial.print(SCK_PIN);
    Serial.print(", MISO=pad ");
    Serial.print(MISO_PIN);
    Serial.print(", MOSI=pad ");
    Serial.println(MOSI_PIN);
    Serial.println("connect MOSI to MISO for the loopback");
}

void loop()
{
    static const uint8_t pattern[] = {0x00, 0x55, 0xAA, 0xFF, 0x5A};

    // en: Apply settings for this transaction; transfer() sends and receives together.
    // ja: このtransactionの設定を適用。transfer()は送受信を同時に行う。
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    for (unsigned i = 0; i < sizeof pattern; i++) {
        // en: The loopback jumper makes the simultaneously received byte equal the transmitted pattern.
        // ja: ジャンパで折り返しているので、同時に受信した値が送信patternと一致するか確認する。
        const uint8_t got = SPI.transfer(pattern[i]);
        Serial.print("sent 0x");
        Serial.print(pattern[i], HEX);
        Serial.print("  got 0x");
        Serial.print(got, HEX);
        Serial.println(got == pattern[i] ? "  ok" : "  (no loopback)");
    }
    SPI.endTransaction();

    Serial.println();
    delay(2000);
}
