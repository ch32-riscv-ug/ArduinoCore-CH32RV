// en: Inspect I2C timeouts and attempt bus recovery only on command.
// ja: I2Cのtimeoutを調べ、明示的な操作でだけバス復旧を試す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: I2C1 */

#include <Wire.h>

static const uint8_t DEVICE_ADDRESS = 0x42;
static bool busReady;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    busReady = Wire.begin(SDA, SCL);
    if (!busReady) {
        Serial.println("I2C begin failed; check pin routes");
        return;
    }
    // en: Microseconds, not milliseconds; retain a finite wait for a stuck bus.
    // ja: 単位はmsではなくus。バス異常時にも待ち時間を有限に保つ。
    Wire.setWireTimeout(25000);
    Serial.println("p: probe 0x42, r: attempt bus recovery");
}

void loop()
{
    if (!busReady || !Serial.available()) {
        return;
    }
    const int command = Serial.read();
    if (command == 'p') {
        // en: Clear the sticky flag before the operation whose timeout we want to inspect.
        // ja: 今回の操作でtimeoutしたかを調べるため、保持されたflagを先に消す。
        Wire.clearWireTimeoutFlag();
        Wire.beginTransmission(DEVICE_ADDRESS);
        const uint8_t error = Wire.endTransmission();
        Serial.print("Probe result: ");
        Serial.println(error);
        Serial.print("Timeout flag: ");
        Serial.println(Wire.getWireTimeoutFlag() ? "set" : "clear");
    } else if (command == 'r') {
        // en: Recovery pulses reach every device; never run this automatically on a shared bus.
        // ja: 復旧clockは全機器へ届く。共有バスで無条件に自動実行しない。
        Serial.println(Wire.clearBus() ? "Bus lines released" : "Bus still held low");
    }
}
