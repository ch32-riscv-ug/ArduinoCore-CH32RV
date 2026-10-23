// en: Echo uppercase text over the DMSeq debug terminal.
// ja: DMSeqのデバッグ端末で、受信文字を大文字にして返す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

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
        // en: Reply through the same debug channel; this is separate from a hardware UART connection.
        // ja: 受信と同じデバッグ経路へ返信する。ハードウェアUARTの接続とは別。
        SerialDMSeq.write((uint8_t)c);
    }

    static unsigned long last;
    if (millis() - last >= 1000) {
        last = millis();
        SerialDMSeq.print("uptime ");
        SerialDMSeq.println(millis() / 1000);
    }
}
