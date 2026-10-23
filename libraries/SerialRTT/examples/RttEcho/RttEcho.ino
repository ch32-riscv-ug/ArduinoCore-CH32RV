// en: Echo uppercase RTT input while keeping an optional LED blinking.
// ja: RTTの受信を大文字で返しながら、LEDも点滅させる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SerialRTT.h>

void setup()
{
// en: Blinking is optional: without a board LED definition, the RTT echo still works.
// ja: 点滅は任意。LEDの定義がないボードでもRTTのエコーは動く。
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif
    SerialRTT.begin(115200);
    SerialRTT.println("type a line; it comes back in capitals");
}

void loop()
{
    // en: Drain available bytes without waiting so LED updates can continue.
    // ja: 受信済みの分だけ処理し、LED更新を待たせない。
    while (SerialRTT.available() > 0) {
        int c = SerialRTT.read();
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        SerialRTT.write((uint8_t)c);
    }

#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, (millis() / 500) % 2 == 0 ? HIGH : LOW);
#endif
}
