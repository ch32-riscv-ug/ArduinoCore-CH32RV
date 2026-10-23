// en: Echo uppercase text over the DMDATA debug terminal.
// ja: DMDATAのデバッグ端末で、受信文字を大文字にして返す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SerialDMDATA.h>

void setup()
{
    SerialDMDATA.begin(115200);
    SerialDMDATA.println("hello from the debug module");
}

void loop()
{
    // en: Poll incoming data before prints overwrite the shared debug register.
    // ja: 表示で共有デバッグレジスタを書き換える前に、受信を取り込む。
    while (SerialDMDATA.available() > 0) {
        int c = SerialDMDATA.read();
        // en: Convert only ASCII lowercase letters; other received byte values pass through unchanged.
        // ja: ASCIIの英小文字だけを変換する。ほかの受信バイトはそのまま返す。
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        SerialDMDATA.write((uint8_t)c);
    }

    static unsigned long last;
    if (millis() - last >= 1000) {
        last = millis();
        SerialDMDATA.print("uptime ");
        SerialDMDATA.print(millis() / 1000);
        SerialDMDATA.println(" s");
    }
}
