// en: Two SPI devices on one bus, each with its own CS, speed and mode: beginTransaction() per device.
// ja: 1本のSPIに2つの機器。CS・速さ・モードはそれぞれ。機器ごとにbeginTransaction()する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: SPI1, PA3, PA4 */

#include <SPI.h>

// en: Two GPIOs for the chip selects; change them to yours.
// ja: CSの2本のGPIO。自分の配線に合わせて変える。
static const uint8_t FLASH_CS = PA4;
static const uint8_t SENSOR_CS = PA3;
static const SPISettings FLASH_SETTINGS(8000000, MSBFIRST, SPI_MODE0);    // en: an SPI Flash / ja: SPI Flash
static const SPISettings SENSOR_SETTINGS(1000000, MSBFIRST, SPI_MODE3);   // en: a slower sensor in mode 3 / ja: モード3の遅いセンサ

static void select(uint8_t cs, const SPISettings &s)
{
    SPI.beginTransaction(s);
    digitalWrite(cs, LOW);
}

static void deselect(uint8_t cs)
{
    digitalWrite(cs, HIGH);
    SPI.endTransaction();
}

void setup()
{
    Serial.begin(115200);
    // en: Both CS high before the bus starts: neither device listens to the other's traffic.
    // ja: バスを始める前に両方のCSをHighに: どちらも相手向けの通信を聞かない。
    pinMode(FLASH_CS, OUTPUT);
    pinMode(SENSOR_CS, OUTPUT);
    digitalWrite(FLASH_CS, HIGH);
    digitalWrite(SENSOR_CS, HIGH);
    SPI.begin();
}

void loop()
{
    uint8_t id[3];
    select(FLASH_CS, FLASH_SETTINGS);
    SPI.transfer(0x9F);                 // en: JEDEC ID / ja: JEDEC ID
    for (uint8_t &b : id) {
        b = SPI.transfer(0);
    }
    deselect(FLASH_CS);

    select(SENSOR_CS, SENSOR_SETTINGS);
    SPI.transfer(0x80 | 0x0F);          // en: read register 0x0F (WHO_AM_I on many sensors) / ja: レジスタ0x0Fを読む（多くのセンサのWHO_AM_I）
    const uint8_t who = SPI.transfer(0);
    deselect(SENSOR_CS);

    Serial.printf("flash %02X %02X %02X, sensor 0x%02X\r\n", id[0], id[1], id[2], who);
    delay(1000);
}
