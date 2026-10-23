// en: Read a register block, checking address errors and the received length.
// ja: アドレスの応答と受信長を確認し、I2Cのレジスタを読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: I2C1 */

#include <Wire.h>

static const uint8_t DEVICE_ADDRESS = 0x42;
static const uint8_t FIRST_REGISTER = 0;
static const uint8_t REGISTER_COUNT = 3;
// en: Our paired slave delivers writes at STOP; choose false only for a compatible external device.
// ja: 対のslaveはSTOPで書き込みを受け取る。falseは対応する外部機器でだけ選ぶ。
static const bool STOP_AFTER_REGISTER = true;
static bool busReady;
static uint32_t lastReadMs;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    busReady = Wire.begin(SDA, SCL);
    if (!busReady) {
        Serial.println("I2C begin failed; check pin routes");
    }
}

void loop()
{
    const uint32_t now = millis();
    if (!busReady || (uint32_t)(now - lastReadMs) < 1000) {
        return;
    }
    lastReadMs = now;
    Wire.beginTransmission(DEVICE_ADDRESS);
    Wire.write(FIRST_REGISTER);
    // en: false keeps the bus for a repeated START; true ends this write with STOP.
    // ja: falseならバスを保持してrepeated STARTへ、trueならSTOPで書き込みを終える。
    const uint8_t error = Wire.endTransmission(STOP_AFTER_REGISTER);
    if (error != 0) {
        Serial.print("Register selection failed, code: ");
        Serial.println(error);
        return;
    }
    const size_t count = Wire.requestFrom(DEVICE_ADDRESS, (size_t)REGISTER_COUNT);
    // en: A successful address write does not guarantee the requested read will complete.
    // ja: アドレス書き込みの成功だけでは、その後の読み取りの完了は保証されない。
    if (count != REGISTER_COUNT) {
        Serial.print("Short read, bytes: ");
        Serial.println((unsigned)count);
        while (Wire.available()) {
            Wire.read();
        }
        return;
    }
    Serial.print("Registers:");
    while (Wire.available()) {
        Serial.print(' ');
        Serial.print(Wire.read(), HEX);
    }
    Serial.println();
}
