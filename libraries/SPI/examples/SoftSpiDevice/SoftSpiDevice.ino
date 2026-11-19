// en: An SPI device on any pins (SoftSPI: SPI's API in software): read an SPI Flash's JEDEC ID.
// ja: どの端子でもSPIの機器を使う（SoftSPI: SPIのAPIをソフトで）。SPI FlashのJEDEC IDを読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: PA1, PA2, PA3, PA4 */

#include <SoftSPI.h>

// en: Any four GPIO pads; change them to yours.
// ja: どのGPIOの4本でもよい。自分の配線に合わせて変える。
static const uint8_t SCK_PIN = PA1, MISO_PIN = PA2, MOSI_PIN = PA3, CS_PIN = PA4;

SoftSPI bus(SCK_PIN, MISO_PIN, MOSI_PIN);

void setup()
{
    Serial.begin(115200);
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    bus.begin();
}

void loop()
{
    bus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CS_PIN, LOW);
    bus.transfer(0x9F);
    uint8_t id[3];
    for (uint8_t &b : id) {
        b = bus.transfer(0);
    }
    digitalWrite(CS_PIN, HIGH);
    bus.endTransaction();
    Serial.printf("JEDEC ID: %02X %02X %02X\r\n", id[0], id[1], id[2]);
    delay(1000);
}
