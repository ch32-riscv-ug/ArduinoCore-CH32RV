// en: Scan software I2C for responding addresses on two chosen GPIOs.
// ja: 選んだ二つのGPIOでソフトウェアI2Cを使い、応答アドレスを探す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SoftWire.h>

static const uint8_t SDA_PIN = PA1;
static const uint8_t SCL_PIN = PA2;

SoftWire bus(SDA_PIN, SCL_PIN);

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    bus.begin();
    bus.setClock(100000);

    Serial.println();
    Serial.print("SoftWire on SDA=pad ");
    Serial.print(SDA_PIN);
    Serial.print(", SCL=pad ");
    Serial.println(SCL_PIN);
}

void loop()
{
    uint8_t found = 0;

    // en: Skip reserved seven-bit addresses.
    // ja: 予約済みの7 bitアドレスは除外する。
    for (uint8_t address = 0x08; address < 0x78; address++) {
        // en: Use a seven-bit address; endTransmission() performs the probe and reports whether it was acknowledged.
        // ja: 7 bitアドレスを指定する。endTransmission()で試行し、相手の応答の有無を読む。
        bus.beginTransmission(address);
        if (bus.endTransmission() == 0) {
            Serial.print("found 0x");
            if (address < 0x10) {
                Serial.print('0');
            }
            Serial.println(address, HEX);
            found++;
        }
    }

    if (found == 0) {
        Serial.println(bus.getWireTimeoutFlag()
                           ? "nothing found, and the bus timed out - pull-ups?"
                           : "nothing found");
        // en: Clear the sticky timeout after reporting it.
        // ja: 保持されたtimeoutを表示後に解除する。
        bus.clearWireTimeoutFlag();
    }
    delay(2000);
}
