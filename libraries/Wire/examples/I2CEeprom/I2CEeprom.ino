// en: Write and read back a 24C02-style I2C EEPROM: page writes, the write-cycle wait by ACK polling.
// ja: 24C02型のI2C EEPROMに書いて読み戻す。ページ単位の書き込みと、ACKのポーリングで書き込み時間を待つ。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: I2C1 */

#include <Wire.h>

static const uint8_t EEPROM_ADDRESS = 0x50;   // en: A2..A0 low / ja: A2〜A0をLow
static const uint8_t PAGE = 8;                // en: 24C02: 8 bytes a page (24C04..16: 16) / ja: 24C02は8バイト（24C04〜16は16）

// en: Waits until the chip answers again after a write (its internal cycle, up to 5 ms).
// ja: 書き込みのあと、チップがまた応答するまで待つ（内部の書き込みは最大5 ms）。
static bool waitReady()
{
    const uint32_t t0 = millis();
    do {
        Wire.beginTransmission(EEPROM_ADDRESS);
        if (Wire.endTransmission() == 0) {
            return true;
        }
    } while (millis() - t0 < 20);
    return false;
}

// en: Writes n bytes from address `at`, never across a page boundary in one transaction.
// ja: アドレスatからnバイトを書く。1回の書き込みはページの境をまたがない。
static bool writeBytes(uint8_t at, const uint8_t *data, size_t n)
{
    while (n) {
        size_t chunk = PAGE - (at % PAGE);
        chunk = chunk < n ? chunk : n;
        Wire.beginTransmission(EEPROM_ADDRESS);
        Wire.write(at);
        Wire.write(data, chunk);
        if (Wire.endTransmission() != 0 || !waitReady()) {
            return false;
        }
        at += chunk;
        data += chunk;
        n -= chunk;
    }
    return true;
}

static bool readBytes(uint8_t at, uint8_t *data, size_t n)
{
    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write(at);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(EEPROM_ADDRESS, n) != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        data[i] = Wire.read();
    }
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Wire.begin();
    const char message[] = "Hello, EEPROM!";
    const bool wrote = writeBytes(5, (const uint8_t *)message, sizeof message);   // en: from 5: crosses a page / ja: 5から: ページをまたぐ
    char back[sizeof message] = {};
    const bool read = readBytes(5, (uint8_t *)back, sizeof back);
    Serial.print(wrote ? "written, " : "write failed, ");
    Serial.print(read ? "read back: " : "read failed: ");
    Serial.println(back);
}

void loop()
{
}
