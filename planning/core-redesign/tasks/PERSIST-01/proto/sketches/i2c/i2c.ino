// en: Expose a small register block with short interrupt callbacks.
// ja: 短い割り込みcallbackで小さなI2Cレジスタを公開する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: I2C1 */

#include <Wire.h>

static const uint8_t DEVICE_ADDRESS = 0x42;
static const uint8_t REGISTER_COUNT = 3;
static uint8_t selectedRegister;
static volatile uint8_t userValue;
static volatile uint8_t receiveCount;
static bool busReady;

static void receiveRegisters(int count)
{
    // en: This runs in an ISR; read the bounded message but do not print or delay here.
    // ja: ISR内で呼ばれる。短い受信だけを扱い、ここで表示や待機をしない。
    if (count <= 0 || !Wire.available()) {
        return;
    }
    uint8_t index = (uint8_t)Wire.read();
    selectedRegister = index;
    while (Wire.available()) {
        const uint8_t value = (uint8_t)Wire.read();
        if (index == 2) {
            userValue = value;
        }
        // en: Saturate the index so a long write cannot wrap into a writable register.
        // ja: 長い書き込みで番号が周回しないよう、範囲外は範囲外のまま保つ。
        if (index < REGISTER_COUNT) {
            ++index;
        }
    }
    ++receiveCount;
}

static void requestRegisters()
{
    // en: Prepare only the remaining registers; an out-of-range read returns FF.
    // ja: 残りのレジスタだけを用意する。範囲外の読み取りにはFFを返す。
    const uint8_t snapshot[REGISTER_COUNT] = {0x32, receiveCount, userValue};
    if (selectedRegister < REGISTER_COUNT) {
        Wire.write(snapshot + selectedRegister, REGISTER_COUNT - selectedRegister);
    } else {
        Wire.write((uint8_t)0xFF);
    }
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Wire.onReceive(receiveRegisters);
    Wire.onRequest(requestRegisters);
    busReady = Wire.begin(DEVICE_ADDRESS, SDA, SCL, 100000);
    Serial.println(busReady ? "Slave ready at 0x42" : "I2C slave begin failed");
}

void loop()
{
    static uint32_t lastPrintMs;
    const uint32_t now = millis();
    if (!busReady || (uint32_t)(now - lastPrintMs) < 1000) {
        return;
    }
    lastPrintMs = now;
    // en: Copy related ISR data in a short critical section; print after interrupts resume.
    // ja: ISRと共有する値を短いcritical sectionでコピーし、割り込みを戻してから表示する。
    uint8_t count, value;
    noInterrupts();
    count = receiveCount;
    value = userValue;
    interrupts();
    Serial.print("Received writes (modulo 256): ");
    Serial.print(count);
    Serial.print(" user register: ");
    Serial.println(value);
}
