// en: Print text through an RTT buffer in RAM for a debug probe to read.
// ja: デバッグプローブが読むRAM上のRTTバッファへ文字列を出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SerialRTT.h>

void setup()
{
    // en: Output uses a finite RAM ring, not a UART baud clock.
    // ja: UARTのbaud clockではなく、容量の限られたRAM ringを使う。
    SerialRTT.begin(115200);
    SerialRTT.println("hello from RAM");
}

void loop()
{
    // en: Write a periodic log to the RAM buffer; the probe reads it independently when its monitor is running.
    // ja: 定期ログをRAMのbufferへ書き、モニタ動作中のプローブが別途読み出す。
    SerialRTT.print("uptime ");
    SerialRTT.print(millis() / 1000);
    SerialRTT.println(" s");
    delay(1000);
}
