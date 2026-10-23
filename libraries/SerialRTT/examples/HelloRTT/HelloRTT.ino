// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <SerialRTT.h>

void setup()
{
    // en: Output uses a finite RAM ring, not a UART baud clock. / ja: UARTのbaud clockではなく、容量の限られたRAM ringを使う。
    SerialRTT.begin(115200);
    SerialRTT.println("hello from RAM");
}

void loop()
{
    SerialRTT.print("uptime ");
    SerialRTT.print(millis() / 1000);
    SerialRTT.println(" s");
    delay(1000);
}
