// en: Read a register of an I2C device on any two pins (SoftWire: Wire's API in software).
// ja: どの2本の端子でも、I2Cの機器のレジスタを読む（SoftWire: WireのAPIをソフトで）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SoftWire.h>

// en: Any two GPIO pads; change them to yours.
// ja: どのGPIOの2本でもよい。自分の配線に合わせて変える。
static const uint8_t SDA_PIN = PA1;
static const uint8_t SCL_PIN = PA2;
static const uint8_t DEVICE_ADDRESS = 0x68;    // en: an MPU-6050 / DS3231, for example / ja: 例: MPU-6050やDS3231
static const uint8_t REGISTER = 0x75;          // en: MPU-6050's WHO_AM_I (0x68) / ja: MPU-6050のWHO_AM_I（0x68）

SoftWire bus(SDA_PIN, SCL_PIN);

void setup()
{
    Serial.begin(115200);
    bus.begin();
    bus.setClock(100000);
}

void loop()
{
    bus.beginTransmission(DEVICE_ADDRESS);
    bus.write(REGISTER);
    const uint8_t error = bus.endTransmission(false);   // en: a repeated START follows / ja: repeated STARTが続く
    if (error != 0) {
        Serial.print("no answer, code ");
        Serial.println(error);
    } else if (bus.requestFrom(DEVICE_ADDRESS, (uint8_t)1) == 1) {
        Serial.print("register 0x");
        Serial.print(REGISTER, HEX);
        Serial.print(" = 0x");
        Serial.println(bus.read(), HEX);
    } else {
        Serial.println("short read");
    }
    delay(1000);
}
