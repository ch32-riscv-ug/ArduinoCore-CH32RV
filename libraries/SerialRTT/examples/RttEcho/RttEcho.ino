// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <SerialRTT.h>

void setup()
{
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif
    SerialRTT.begin(115200);
    SerialRTT.println("type a line; it comes back in capitals");
}

void loop()
{
    // en: Drain available bytes without waiting so LED updates can continue. / ja: 受信済みの分だけ処理し、LED更新を待たせない。
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
