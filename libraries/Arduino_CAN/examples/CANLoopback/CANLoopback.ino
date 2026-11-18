// en: Check the CAN controller with no bus: loopback and silent mode, a frame sent comes back.
// ja: バス無しでCANコントローラを確かめる。ループバックとサイレントで、送ったフレームが戻る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CAN1 */

#include <Arduino_CAN.h>

void setup()
{
    Serial.begin(115200);
    CAN.setLoopback(true, true);
    if (!CAN.begin(CanBitRate::BR_500k)) {
        Serial.println("CAN.begin() failed");
    }
}

void loop()
{
    static uint8_t n;
    uint8_t data[4] = {0xCA, 0xFE, n, (uint8_t)~n};
    CAN.write(CanMsg(CanStandardId(0x123), sizeof data, data));
    delay(10);
    while (CAN.available()) {
        Serial.println(CAN.read());     // [123] (4) : CAFE....
    }
    n++;
    delay(1000);
}
