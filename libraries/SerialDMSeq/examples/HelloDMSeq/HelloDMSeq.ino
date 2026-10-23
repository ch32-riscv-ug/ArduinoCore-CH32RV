// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <SerialDMSeq.h>

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.println("hello from the debug module");
}

void loop()
{
    // en: Polling also requests host input when the sketch has nothing to print.
    // ja: 表示がないときも、pollでホストからの入力を要求する。
    while (SerialDMSeq.available() > 0) {
        int c = SerialDMSeq.read();
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        SerialDMSeq.write((uint8_t)c);
    }

    static unsigned long last;
    if (millis() - last >= 1000) {
        last = millis();
        SerialDMSeq.print("uptime ");
        SerialDMSeq.println(millis() / 1000);
    }
}
